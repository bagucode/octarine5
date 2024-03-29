#ifndef octarine
#define octarine

// ## 01 ## Standard library includes
#include <iostream>
#include <string>
#include <cassert>
#include <cstdlib>
#include <exception>
#include <cstring>
#include <memory>

// ## 02 ## LLVM includes
#include <llvm/ExecutionEngine/JIT.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/LLVMContext.h>
#include <llvm/Module.h>

// ## 03 ## Platform includes
#ifdef _WIN32
#include <Windows.h>
#elif defined (__APPLE__)
#include <pthread.h>
#include <libkern/OSAtomic.h>
#include <mach/mach_time.h>
#include <time.h>
#include <unistd.h>
#endif

namespace octarine {

	// ## 04 ## Primitives and flags
	#ifdef _WIN32

	typedef __int8           I8;
	typedef unsigned __int8  U8;
	typedef __int16          I16;
	typedef unsigned __int16 U16;
	typedef __int32          I32;
	typedef unsigned __int32 U32;
	typedef __int64          I64;
	typedef unsigned __int64 U64;
	typedef float            F32;
	typedef double           F64;

	typedef U8 Bool;
	typedef I32 Char;

	#ifdef _WIN64
	#define OCT_64
	typedef I64 Word;
	typedef U64 Uword;
	#else
	#define OCT_32
	typedef I32 Word;
	typedef U32 Uword;
	#endif

	#ifdef _DEBUG
	#define OCT_DEBUG
	#endif

	#elif defined (__APPLE__)

	typedef int8_t   I8;
	typedef uint8_t  U8;
	typedef int16_t  I16;
	typedef uint16_t U16;
	typedef int32_t  I32;
	typedef uint32_t U32;
	typedef int64_t  I64;
	typedef uint64_t U64;
	typedef float    F32;
	typedef double   F64;

	typedef U8 Bool;
	typedef I32 Char;

	#ifdef __LP64__
	#define OCT_64
	typedef I64 Word;
	typedef U64 Uword;
	#else
	#define OCT_32
	typedef I32 Word;
	typedef U32 Uword;
	#endif

	#ifndef NDEBUG
	#define OCT_DEBUG
	#endif

	#else

	#endif

	// ## 05 ## Platform specific code
	#ifdef _WIN32
	class System {
	private:
		double timerFreq;
	public:
		template <typename T>
		class ThreadLocal {
		private:
			DWORD _index;
		public:
			ThreadLocal(T* val = nullptr) {
				_index = TlsAlloc();
				TlsSetValue(_index, val);
			}
			~ThreadLocal() {
				TlsFree(_index);
			}
			T* get() const {
				return (T*)TlsGetValue(_index);
			}
			void set(T* val) {
				TlsSetValue(_index, val);
			}
		};
		System() {
			LARGE_INTEGER freq;
			QueryPerformanceFrequency(&freq);
			timerFreq = double(freq.QuadPart) / 1000000000.0;
		}
		~System() { }
		void* alloc(Uword size) {
			void* place = ::malloc(size);
			if(!place) {
				throw std::bad_alloc();
			}
			return place;
		}
		void free(void* place) {
			::free(place);
		}
		void atomicSetUword(volatile Uword* place, Uword value) {
			*place = value;
			MemoryBarrier();
		}
		Uword atomicGetUword(volatile Uword* place) {
			Uword val = *place;
			MemoryBarrier();
			return val;
		}
		bool atomicCompareExchangeUword(volatile Uword* place, Uword expected, Uword newValue) {
			return InterlockedCompareExchange(place, newValue, expected) == expected;
		}
		U64 nanoTimestamp() {
			LARGE_INTEGER now;
			QueryPerformanceCounter(&now);
			return U64(double(now.QuadPart)/timerFreq);
		}
		void sleep(Uword millis) {
			Sleep((DWORD)millis);
		}
		void sleepNanos(Uword nanos) {
			// No such function on windows so we cobble something together using Sleep(0) and high resolution timers
			U64 start = nanoTimestamp();
			while(nanoTimestamp() - start < nanos) {
				Sleep(0);
			}
		}
	};
    #elif defined (__APPLE__)
	class System {
	private:
        mach_timebase_info_data_t _timebaseInfo;
	public:
		template <typename T>
		class ThreadLocal {
		private:
			pthread_key_t _key;
		public:
			ThreadLocal(T* val = nullptr) {
                pthread_key_create(&_key, nullptr);
                pthread_setspecific(_key, val);
			}
			~ThreadLocal() {
				pthread_key_delete(_key);
			}
			T* get() const {
                return (T*)pthread_getspecific(_key);
			}
			void set(T* val) {
                pthread_setspecific(_key, val);
			}
		};
		System() {
            mach_timebase_info(&_timebaseInfo);
		}
		~System() { }
		void* alloc(Uword size) {
			void* place = ::malloc(size);
			if(!place) {
				throw std::bad_alloc();
			}
			return place;
		}
		void free(void* place) {
			::free(place);
		}
		void atomicSetUword(volatile Uword* place, Uword value) {
            #ifdef OCT_64
                while(true) {
                    Uword old = *place;
                    if(OSAtomicCompareAndSwap64Barrier((int64_t)old, (int64_t)value, (volatile int64_t*)place)) {
                        break;
                    }
                }
            #endif
		}
		Uword atomicGetUword(volatile Uword* place) {
            #ifdef OCT_64
                while(true) {
                    Uword val = *place;
                    if(OSAtomicCompareAndSwap64Barrier((int64_t)val, (int64_t)val, (volatile int64_t*)place)) {
                        return val;
                    }
                }
            #endif
		}
		bool atomicCompareExchangeUword(volatile Uword* place, Uword expected, Uword newValue) {
            #ifdef OCT_64
                return OSAtomicCompareAndSwap64Barrier((int64_t)expected, (int64_t)newValue, (volatile int64_t*)place);
            #endif
		}
		U64 nanoTimestamp() {
			U64 ts = mach_absolute_time();
            ts *= _timebaseInfo.numer;
            ts /= _timebaseInfo.denom;
			return ts;
		}
		void sleep(Uword millis) {
            usleep(millis * 1000);
		}
		void sleepNanos(U64 nanos) {
            timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = nanos;
            nanosleep(&ts, nullptr);
		}
	};
	#endif

	static System SYS;

	// ## 05.01 ## Forward declarations
	class Context;
	struct Type;
	struct Namespace;
	template <typename TSelf>
	struct HashtableKey;
	struct Nothing;

	// ## 07 ## Global constants
	const Bool True = 1;
	const Bool False = 0;

	// ## 08 ## Template functions and values
	namespace t {

		template <typename T>
		struct is_protocol {
			static const bool value = false;
		};

	} // namespace t

	// ## 06 ## Declarations

	// DEC Unknown. Use this type when the type is known only at runtime
	struct Unknown { };

	// DEC Nothing. A type that, with variadic types, replaces the use of null pointers.
#ifdef __APPLE__
	struct Nothing { Nothing() {} };
#elif defined _WIN32
	struct Nothing { };
#endif
	static const Nothing nil;

	// DEC OwnageType for pointers
	enum OwnageType {
		OWNED = 0,
		BORROWED,
		MANAGED,
		CONSTANT
	};

	// DEC Pointer
	template <OwnageType OT, typename T, bool is_pobject>
	struct PointerBase {
		T* obj;

		static const OwnageType ownage = OT;
		static const Bool pobject = False;
		T* operator->() { return obj; }
		void dtor(Context* ctx);
	};

	template <OwnageType OT, typename T>
	struct PointerBase<OT, T, true> {
		T obj;

		static const OwnageType ownage = OT;
		static const Bool pobject = True;
		T* operator->() { return obj; }
		void dtor(Context* ctx);
	};

	template <OwnageType OT, typename T>
	struct Pointer : PointerBase<OT, T, t::is_protocol<T>::value> { };

	template <typename T>
	struct Borrowed : Pointer<BORROWED, T> { };

	template <typename T>
	struct Owned : Pointer<OWNED, T> { };

	template <typename T>
	struct Managed : Pointer<MANAGED, T> { };

	template <typename T>
	struct Constant : Pointer<CONSTANT, T> { };
	
	// DEC Array
	template <typename T>
	struct Array {
		Type* elementType;
		Uword size;
		T data[];
	};

	// DEC FixedSizeArray
	template <typename T, Uword size>
	struct FixedSizeArray {
		Type* elementType;
		T data[size];
	};

	// DEC ExchangeHeap
	class ExchangeHeap {
	private:
	public:
		ExchangeHeap();
		~ExchangeHeap();
		template <typename T>
		Owned<T> alloc(Context* ctx);
		template <typename T>
		Owned< Array<T> > allocArray(Context* ctx, Uword length);
		void free(void* object);
	};

	// DEC Option
	template <typename T>
	struct Option {
		enum Variant {
			NOTHING = 0,
			SOMETHING
		};
		Variant variant;
		union {
			Nothing nothing;
			T value;
		};
		bool hasValue();
		T getValue();
	};

	// DEC Hashtable
	template <typename TKey, typename TVal>
	struct HashtableEntry {
		Option<TKey> key;
		TVal val;
	};

	template <typename TKey, typename TVal>
	struct Hashtable {
		Owned< Array< HashtableEntry<HashtableKey<TKey>, TVal> > > entries;
        void ctor(Context* ctx);
		void dtor(Context* ctx);
		void put(TKey key, TVal val);
		Option<TVal> get(TKey key); // TODO: borrow a value instead of removing it
	};

	// DEC String. UTF-8 encoded character sequence.
	struct String {
		Uword numCodepoints;
		Owned< Array<U8> > data;
		static String createFromCString(Context* ctx, const char* str);
	};

	// DEC Runtime
	class Runtime {
	private:
		llvm::LLVMContext _llvmContext;
		llvm::Module* _jitModule;
		llvm::ExecutionEngine* _ee;
		ExchangeHeap _exchangeHeap;
		System::ThreadLocal<Context> _currentContext;
		Hashtable< String, Owned<Namespace> > _namespaces;
		std::vector<Context*> _contexts;

		Runtime(const Runtime& other);
		Runtime(Runtime&& other);
		Runtime& operator=(const Runtime& other);
		Runtime& operator=(Runtime&& other);
	public:
		Runtime();
		~Runtime();
		ExchangeHeap& getExchangeHeap();
		Context* getCurrentContext();
	};

	// DEC Context
	class Context {
	private:
		Runtime* _rt;
		Namespace* _ns;
	public:
		Context(Runtime* rt, Namespace* ns);
		~Context();
		Namespace* getNamespace() const;
		void setNamespace(Namespace* ns);
        Runtime* getRuntime() const;
	};

	// DEC Type
	struct Type {
	};

	// DEC ProtocolObject
	template <typename TS, typename TVT>
	struct ProtocolObject {
		typedef TS TSelf;
		typedef TVT TVTable;
		TS* self;
		TVT* vtable;
	};
	
	// DEC Object protocol
	template <typename T>
	struct ObjectFunctions {
		void (*dtor)(Context* ctx, Borrowed<T> self);
		void (*gc_mark)(Context* ctx, Borrowed<T> self);
	};

	template <typename T>
	struct ObjectVTable {
		Type* type;
		ObjectFunctions<T> fns;
	};

	template <typename T>
	struct Object : ProtocolObject<T, ObjectVTable<T> > {
		void dtor(Context* ctx);
		void gc_mark(Context* ctx);
	};

	// DEC EqComparable protocol
	template <typename T>
	struct EqComparableFunctions {
		Bool (*equals)(Context* ctx, T self, Borrowed< Object<T> > other);
	};

	template <typename T>
	struct EqComparableVTable {
		Type* type;
		EqComparableFunctions<T> fns;
	};

	template <typename T>
	struct EqComparable : ProtocolObject<T, EqComparableVTable<T> > {
		Bool equals(Context* ctx, Borrowed< Object<T> > other);
	};

	// DEC Hashable protocol
	template <typename T>
	struct HashableFunctions {
		Uword (*hash)(Context* ctx, Borrowed<T> self);
	};

	template <typename T>
	struct HashableVTable {
		Type* type;
		HashableFunctions<T> fns;
	};

	template <typename T>
	struct Hashable : ProtocolObject<T, HashableVTable<T> > {
		Uword hash(Context* ctx);
	};

	// DEC HashtableKey protocol
	template <typename T>
	struct HashtableKeyFunctions {
		EqComparableFunctions<T> a;
		HashableFunctions<T> b;
	};

	template <typename T>
	struct HashtableKeyVTable {
		Type* type;
		HashtableKeyFunctions<T> fns;
	};

	template <typename T>
	struct HashtableKey : ProtocolObject<T, HashtableKeyVTable<T> > {
		Uword hash(Context* ctx);
		Bool equals(Context* ctx, Borrowed< Object<T> > other);
	};

	// DEC ManagedBox
	struct ManagedBoxHeader {
		Uword gcMarked; // This is a Uword to make the header pointer aligned, do not switch to bool
	};

	template <typename T>
	struct ManagedBox {
		ManagedBoxHeader header;
		T object; // <-- Pointers to the object (or self field in protocol objects) point here
		static ManagedBox<T>* getBox(T* object);
	};

	// DEC OwnedBox
	struct OwnedBoxHeader {
		Uword dummy; // TODO: remove this header?
	};

	template <typename T>
	struct OwnedBox {
		OwnedBoxHeader header;
		T object; // <-- Pointers to the object (or self field in protocol objects) point here
		static OwnedBox<T>* getBox(T* object);
	};

	// DEC NamespaceEntry
	struct NamespaceEntry {
		enum Variant {
			NOTHING,
			OWNED_OBJECT,
			CONSTANT_OBJECT
		};
		Variant variant;
		union {
			Nothing nothing;
			Owned< Object<Unknown> > owned;
			Constant< Object<Unknown> > constant;
		};
		bool isNothing();
		bool isOwned();
		bool isConstant();
		Variant getVariant();
		Owned< Object<Unknown> > getOwnedObject();
		Constant< Object<Unknown> > getConstantObject();
		void dtor(Context* ctx);
	};

	// DEC Namespace
	struct Namespace {
		String name;
		Hashtable< String, Object<Unknown> > bindings;
		void dtor(Context* ctx);
	};

    // DEC Exception
    class Exception {
		// TODO: everything :)
	};
    
	// DEC End

	// ## 09 ## Definitions

	// DEF Object protocol. Must be satisfied by all octarine types.
    template <typename T>
	void Object<T>::dtor(Context* ctx) {
		this->vtable->fns.dtor(ctx, this->self);
	}
	
	template <typename T>
	void Object<T>::gc_mark(Context *ctx) {
		this->vtable->fns.gc_mark(ctx, this->self);
	}

	namespace t {
		template <typename T>
		struct is_protocol< Object<T> > {
			static const bool value = true;
		};
	}

	// DEF EqComparable protocol.
	template <typename T>
	Bool EqComparable<T>::equals(Context* ctx, Borrowed<Object<T> > other) {
		return this->vtable->fns.equals(ctx, this->self, other);
	};

	namespace t {
		template <typename T>
		struct is_protocol< EqComparable<T> > {
			static const bool value = true;
		};
	}

	// DEF OwnedBox
	template <typename T>
	OwnedBox<T>* OwnedBox<T>::getBox(T* object) {
		return (OwnedBox<T>*)(((U8*)object) - sizeof(OwnedBoxHeader));
	}

	// DEF ManagedBox
	template <typename T>
	ManagedBox<T>* ManagedBox<T>::getBox(T* object) {
		return (ManagedBox<T>*)(((U8*)object) - sizeof(ManagedBoxHeader));
	}

	// DEF ExchangeHeap
	ExchangeHeap::ExchangeHeap() {
	}

	ExchangeHeap::~ExchangeHeap() {
	}

	template <typename T>
	Owned<T> ExchangeHeap::alloc(Context* ctx) {
		OwnedBox<T>* box = (OwnedBox<T>*)SYS.alloc(sizeof(OwnedBox<T>));
		if(!box) {
			throw Exception(); // TODO: message
		}
		Owned<T> ret;
		ret.obj = &box->object;
		return ret;
	}
	
	template <typename T>
	Owned< Array<T> > ExchangeHeap::allocArray(Context* ctx, Uword length) {
		OwnedBox< Array<T> >* box = (OwnedBox< Array<T> >*)SYS.alloc(sizeof(Array<T>) + sizeof(T) * length);
		if(!box) {
			throw Exception(); // TODO: message
		}
		Owned< Array<T> > ret;
		ret.obj = &box->object;
		return ret;
	}
	
	void ExchangeHeap::free(void* object) {
		// Cast to nothing to please template. Type does not matter here, only THE BOX.
		SYS.free(OwnedBox<Nothing>::getBox((Nothing*)object));
	}

	// TODO: Managed Heap

	// DEF Runtime
	static volatile Uword didLLVMInit = False;
	static volatile Uword doingLLVMInit = False;

	Runtime::Runtime() {
		if(SYS.atomicCompareExchangeUword(&doingLLVMInit, False, True)) {
			bool result = true;
			if(!SYS.atomicGetUword(&didLLVMInit)) {
				llvm::InitializeNativeTarget();
				SYS.atomicSetUword(&didLLVMInit, True);
			}
			SYS.atomicSetUword(&doingLLVMInit, False);
			if(!result) {
				throw Exception();
			}
		}
		else {
			// Someone else is doing LLVM init, wait for them to complete
			while(SYS.atomicGetUword(&doingLLVMInit)) {
				SYS.sleep(0);
			}
		}
		// Init LLVM
		// Use placement new and allocate in exchange heap?
		_jitModule = new llvm::Module("JITModule", _llvmContext);
		_ee = llvm::ExecutionEngine::createJIT(_jitModule);
		assert(_ee && "Could not create JIT compiler. Unsupported platform?");

		// Create octarine namespace and the main thread context
		Owned<Namespace> octNs = _exchangeHeap.alloc<Namespace>(nullptr);
		Context* mainCtx = new Context(this, octNs.obj);
		octNs->name = String::createFromCString(mainCtx, "octarine");
		_namespaces.put(octNs->name, octNs);
		_contexts.push_back(mainCtx);
		_currentContext.set(mainCtx);
	}
	
	Runtime::~Runtime() {
		// delete all contexts
		std::vector<Context*>::iterator ci;
		for(ci = _contexts.begin(); ci != _contexts.end(); ++ci) {
			delete (*ci);
		}
		// delete all namespaces
		_namespaces.dtor(nullptr);
		// delete LLVM execution engine; this also deletes the JIT module
		delete _ee;
	}
	
	ExchangeHeap& Runtime::getExchangeHeap() {
		return _exchangeHeap;
	}

	Context* Runtime::getCurrentContext() {
		return _currentContext.get();
	}

	// DEF NamespaceEntry
	bool NamespaceEntry::isNothing() {
		return variant == NOTHING;
	}

	bool NamespaceEntry::isOwned() {
		return variant == OWNED_OBJECT;
	}

	bool NamespaceEntry::isConstant() {
		return variant == CONSTANT_OBJECT;
	}

	NamespaceEntry::Variant NamespaceEntry::getVariant() {
		return variant;
	}

	Owned< Object<Unknown> > NamespaceEntry::getOwnedObject() {
		return owned;
	}

	Constant< Object<Unknown> > NamespaceEntry::getConstantObject() {
		return constant;
	}

	void NamespaceEntry::dtor(Context* ctx) {
		if(isOwned()) {
			owned.dtor(ctx);
		}
	}

	// DEF Context
	Context::Context(Runtime* rt, Namespace* ns): _rt(rt), _ns(ns) {
	}
	
	Context::~Context() {
	}
	
	Namespace* Context::getNamespace() const {
		return _ns;
	}

	void Context::setNamespace(Namespace* ns) {
		_ns = ns;
	}
    
    Runtime* Context::getRuntime() const {
        return _rt;
    }

	// DEF Hashable
	template <typename T>
	Uword Hashable<T>::hash(Context* ctx) {
		return this->vtable->fns.hash(ctx, this->self);
	}

	// DEF HashtableKey
	template <typename T>
	Uword HashtableKey<T>::hash(Context* ctx) {
		return this->vtable->fns.b.hash(ctx, this->self);
	}

	template <typename T>
	Bool HashtableKey<T>::equals(Context* ctx, Borrowed< Object<T> > other) {
		return this->vtable->fns.a.equals(ctx, this->self, other);
	}
    
    // DEF Hashtable
    template <typename TKey, typename TVal>
    void Hashtable<TKey, TVal>::put(TKey key, TVal val) {
#error implement!
        
    }
    
    template <typename TKey, typename TVal>
    Option<TVal> Hashtable<TKey, TVal>::get(TKey key) {
#error implement!
        
    }
    
    template <typename TKey, typename TVal>
    void Hashtable<TKey, TVal>::ctor(Context* ctx) {
        entries = ctx->getRuntime()->getExchangeHeap().allocArray<HashtableEntry< HashtableKey<TKey>, TVal> >(ctx, 100);
    }

    template <typename TKey, typename TVal>
    void Hashtable<TKey, TVal>::dtor(Context* ctx) {
        entries.dtor(ctx);
    }


	// DEF Option
	template <typename T>
	bool Option<T>::hasValue() {
		return variant == SOMETHING;
	}

	template <typename T>
	T Option<T>::getValue() {
		if(variant == NOTHING) {
			throw Exception(); // TODO: message
		}
		return value;
	}

    // DEF String
    String String::createFromCString(Context* ctx, const char* str) {
        Uword len = strlen(str);
        String s;
        s.data = ctx->getRuntime()->getExchangeHeap().allocArray<U8>(ctx, len + 1);
        memcpy(&s.data->data[0], str, len);
        s.data->data[len] = '\0';
        s.data->size = len + 1;
        s.numCodepoints = len; // This is not correct. Need to account for multibyte chars.
        return s;
    }
    
	// DEF End

} // namespace octarine

#ifdef OCT_EMBED

extern "C" {

	// TODO: C-API goes here

} // extern "C"

#else

extern "C" {

	int main(int argv, char* argc[]) {
		octarine::Runtime rt;

		// TODO: implement repl

		return 0;
	}

} // extern "C"

#endif

#endif // #ifndef octarine
