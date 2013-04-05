#ifndef octarine
#define octarine

// ## 01 ## Standard library includes
#include <iostream>
#include <string>
#include <cassert>
#include <cstdlib>
#include <exception>

// ## 02 ## LLVM includes
#include <llvm/ExecutionEngine/JIT.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/LLVMContext.h>
#include <llvm/Module.h>

// ## 03 ## Platform includes
#ifdef _WIN32
#include <Windows.h>
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
		Uword nanoTimestamp() {
			LARGE_INTEGER now;
			QueryPerformanceCounter(&now);
			return Uword(double(now.QuadPart)/timerFreq);
		}
		void sleep(Uword millis) {
			Sleep((DWORD)millis);
		}
		void sleepNanos(Uword nanos) {
			// No such function on windows so we cobble something together using Sleep(0) and high resolution timers
			Uword start = nanoTimestamp();
			while(nanoTimestamp() - start < nanos) {
				Sleep(0);
			}
		}
	};
	#endif

	static System SYS;

	// ## 05.01 ## Forward declarations
	class Context;
	struct Type;
	class Namespace;
	template <typename TSelf>
	struct HashtableKey;
	struct Nothing;

	// ## 07 ## Global constants
	const Bool True = 1;
	const Bool False = 0;

	// ## 08 ## Template functions and values
	namespace t {

		template <typename T>
		struct destroy {
			// static void call(T* self); // Must not throw exceptions
		};

		template <typename T>
		struct gc_mark {
			// static void set(Context* ctx, T* self); // Must not throw exceptions
			// static bool get(Context* ctx, T* self); // Must not throw exceptions
		};

		template <typename T>
		struct borrow_count {
			//static void inc(T*);
			//static void dec(T*);
		};

		// An owner type of Nothing indicates that the value pointed to
		// is a constant and thus, inc and dec are noops.
		template <>
		struct borrow_count<Nothing> {
			static void inc(Nothing*) { }
			static void dec(Nothing*) { }
		};

		template <typename T>
		struct is_protocol {
			static const bool value = false;
		};

		template <typename T>
		struct as_hashtableKey {
		};

	} // namespace t

	// ## 06 ## Declarations

	// DEC Unknown. Use this type when the type is known only at runtime
	struct Unknown { };

	// DEC Nothing. A type that, with variadic types, replaces the use of null pointers.
	struct Nothing { };
	static const Nothing nil;

	// DEC OwnageType for pointers
	enum OwnageType {
		OWNED = 0,
		BORROWED,
		MANAGED,
		CONSTANT
	};

	// DEC Pointer
	template <OwnageType OT, typename T, bool pobject>
	struct PointerBase {
		T* obj;

		static const OwnageType ownage = OT;
		static const Bool pobject = False;
		T* operator->() { return obj; }
	};

	template <OwnageType OT, typename T>
	struct PointerBase<OT, T, true> {
		T obj;

		static const OwnageType ownage = OT;
		static const Bool pobject = True;
		T* operator->() { return obj; }
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
	
	//// DEC Borrowed
	//template <typename T>
	//struct Borrowed {
	//	T* obj;

	//	static const OwnageType ownage = BORROWED;
	//	T* operator->() { return obj; }
	//};

	//// DEC Owned
	//template <typename T>
	//struct Owned {
	//	T* obj;

	//	static const OwnageType ownage = OWNED;
	//	T* operator->() { return obj; }
	//};

	//template <typename T>
	//struct Owned< PObject<T> > {
	//	PObject<T> obj;

	//	static const OwnageType ownage = OWNED_POBJECT;
	//	T* operator->() { return obj; }
	//};

	//// DEC Managed
	//template <typename T>
	//struct Managed {
	//	T* obj;

	//	static const OwnageType ownage = MANAGED;
	//	T* operator->() { return obj; }
	//};

	//// DEC Constant
	//template <typename T>
	//struct Constant {
	//	T* obj;

	//	static const OwnageType ownage = CONSTANT;
	//	T* operator->() { return obj; }
	//};

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
		void put(TKey key, TVal val);
		Option<TVal> get(TKey key); // TODO: borrow a value instead of removing it
	};

	// DEC String. UTF-8 encoded character sequence.
	struct String {
		Uword numCodepoints;
		Owned< Array<U8> > data;
		static String createFromCString(const char* str);
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
		Namespace* getNamespace();
		void setNamespace(Namespace* ns);
	};

	// DEC Type
	struct Type {
	};

	// DEC ProtocolObject
	template <typename TSelf, typename TVTable>
	struct ProtocolObject {
		typedef TSelf TSelf;
		typedef TVTable TVTable;
		TSelf* self;
		TVTable* vtable;
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
		bool gc_marked;
		Uword borrowCount;
		Type* type;
	};

	template <typename T>
	struct ManagedBox {
		ManagedBoxHeader header;
		T object; // <-- Pointers to the object (or self field in protocol objects) point here
		static ManagedBox<T>* getBox(T* object);
	};

	// DEC OwnedBox
	struct OwnedBoxHeader {
		Context* ctx;
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
		union Value {
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
	};

	// DEC Namespace
	struct Namespace {
		String name;
		Hashtable< String, Object<Unknown> > bindings;
	};

	// DEC End

	// ## 09 ## Definitions

	// DEF Object protocol. Must be satisfied by all octarine types.
	template <typename T>
	void Object<T>::dtor(Context* ctx) {
		vtable->fns.dtor(ctx, self);
	}
	
	template <typename T>
	void Object<T>::gc_mark(Context* ctx) {
		vtable->fns.gc_mark(ctx, self);
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
		return vtable->fns.equals(ctx, self, other);
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
		box->header.ctx = ctx;
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
		box->header.ctx = ctx;
		Owned< Array<T> > ret;
		ret.obj = &box->object;
		return ret;
	}
	
	void ExchangeHeap::free(void* object) {
		// Cast to nothing to please template. Type does not matter here, only THE BOX.
		SYS.free(OwnedBox<Nothing>::getBox((Nothing*)object));
	}

	// TODO: Managed Heap

	class Exception {
		// TODO: everything :)
	};

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
		// Create octarine Namespace

		Owned<Namespace> octNs = _exchangeHeap.alloc<Namespace>(nullptr);
		octNs->name = String::createFromCString("octarine");
		_namespaces.put(octNs->name, octNs);
		// Create main Context
		Context* mainCtx = new Context(this, octNs.obj);
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
		Uword i;
		for(i = _namespace; ni != _namespaces.end(); ++ni) {
			delete ni->second;
		}
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
	NamespaceEntry::NamespaceEntry(Runtime* rt, Variant variant, Value value):
		_rt(rt), _variant(variant), _value(value) {
	}

	NamespaceEntry::NamespaceEntry(NamespaceEntry&& other):
		_rt(other._rt), _variant(other._variant), _value(other._value) {
		other._variant = NOTHING;
	}

	NamespaceEntry::~NamespaceEntry() {
		if(_variant == OWNED_OBJECT) {
			_value.owned.dtor(_rt->getCurrentContext());
			_rt->getExchangeHeap().free(_value.owned.self.obj);
		}
	}

	NamespaceEntry& NamespaceEntry::operator=(NamespaceEntry&& other) {
		_rt = std::move(other._rt);
		_variant = std::move(other._variant);
		_value = std::move(other._value);
		other._variant = NOTHING;
		return *this;
	}

	bool NamespaceEntry::isNothing() {
		return _variant == NOTHING;
	}

	bool NamespaceEntry::isOwned() {
		return _variant == OWNED_OBJECT;
	}

	bool NamespaceEntry::isConstant() {
		return _variant == CONSTANT_OBJECT;
	}

	NamespaceEntry::Variant NamespaceEntry::getVariant() {
		return _variant;
	}

	Object<Owned> NamespaceEntry::getOwnedObject() {
		return _value.owned;
	}

	Object<Constant> NamespaceEntry::getConstantObject() {
		return _value.constant;
	}

	// DEF Context
	Context::Context(Runtime* rt, Namespace* ns): _rt(rt), _ns(ns) {
	}
	
	Context::~Context() {
	}
	
	Namespace* Context::getNamespace() {
		return _ns;
	}

	void Context::setNamespace(Namespace* ns) {
		_ns = ns;
	}

	// DEF Namespace
	Namespace::Namespace(const std::string& name): _name(name) {
	}

	Namespace::~Namespace() {
	}

	std::string Namespace::getName() {
		return _name;
	}

	// DEF Hashable
	template <typename T>
	Uword Hashable<T>::hash(Context* ctx) {
		return vtable->fns.hash(ctx, self);
	}

	// DEF HashtableKey
	template <typename T>
	Uword HashtableKey<T>::hash(Context* ctx) {
		return vtable->fns.b.hash(ctx, self);
	}

	template <typename T>
	Bool HashtableKey<T>::equals(Context* ctx, Object<Borrowed> other) {
		return vtable->fns.a.equals(ctx, self, other);
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
