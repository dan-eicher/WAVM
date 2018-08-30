#pragma once

#include "IR/Module.h"
#include "IR/Operators.h"
#include "Inline/BasicTypes.h"
#include "Intrinsics.h"
#include "RuntimePrivate.h"

#include <cctype>
#include <string>
#include <vector>

#include "LLVMPreInclude.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/DataTypes.h"

#include "LLVMPostInclude.h"

#ifdef _WIN32
#define USE_WINDOWS_SEH 1
#else
#define USE_WINDOWS_SEH 0
#endif

namespace llvm
{
	class LoadedObjectInfo;

	namespace object
	{
		class SectionRef;
	}
}

namespace LLVMJIT
{
	typedef llvm::SmallVector<llvm::Value*, 1> ValueVector;
	typedef llvm::SmallVector<llvm::PHINode*, 1> PHIVector;

	// The global LLVM context.
	extern llvm::LLVMContext* llvmContext;

	// Maps a type ID to the corresponding LLVM type.
	extern llvm::Type* llvmValueTypes[Uptr(IR::ValueType::num)];

	extern llvm::Type* llvmI8Type;
	extern llvm::Type* llvmI16Type;
	extern llvm::Type* llvmI32Type;
	extern llvm::Type* llvmI64Type;
	extern llvm::Type* llvmI128Type;
	extern llvm::Type* llvmF32Type;
	extern llvm::Type* llvmF64Type;
	extern llvm::Type* llvmVoidType;
	extern llvm::Type* llvmBoolType;
	extern llvm::Type* llvmI8PtrType;
	extern llvm::Type* llvmIptrType;

	extern llvm::Type* llvmI8x16Type;
	extern llvm::Type* llvmI16x8Type;
	extern llvm::Type* llvmI32x4Type;
	extern llvm::Type* llvmI64x2Type;
	extern llvm::Type* llvmI128x1Type;
	extern llvm::Type* llvmF32x4Type;
	extern llvm::Type* llvmF64x2Type;

	extern llvm::Type* llvmExceptionPointersStructType;

	// Zero constants of each type.
	extern llvm::Constant* typedZeroConstants[(Uptr)IR::ValueType::num];

	extern Platform::Mutex llvmMutex;

	extern void initLLVM();

	// Overloaded functions that compile a literal value to a LLVM constant of the right type.
	inline llvm::ConstantInt* emitLiteral(U32 value)
	{
		return (llvm::ConstantInt*)llvm::ConstantInt::get(llvmI32Type,
														  llvm::APInt(32, (U64)value, false));
	}
	inline llvm::ConstantInt* emitLiteral(I32 value)
	{
		return (llvm::ConstantInt*)llvm::ConstantInt::get(llvmI32Type,
														  llvm::APInt(32, (I64)value, false));
	}
	inline llvm::ConstantInt* emitLiteral(U64 value)
	{
		return (llvm::ConstantInt*)llvm::ConstantInt::get(llvmI64Type,
														  llvm::APInt(64, value, false));
	}
	inline llvm::ConstantInt* emitLiteral(I64 value)
	{
		return (llvm::ConstantInt*)llvm::ConstantInt::get(llvmI64Type,
														  llvm::APInt(64, value, false));
	}
	inline llvm::Constant* emitLiteral(F32 value)
	{
		return llvm::ConstantFP::get(*llvmContext, llvm::APFloat(value));
	}
	inline llvm::Constant* emitLiteral(F64 value)
	{
		return llvm::ConstantFP::get(*llvmContext, llvm::APFloat(value));
	}
	inline llvm::Constant* emitLiteral(bool value)
	{
		return llvm::ConstantInt::get(llvmBoolType, llvm::APInt(1, value ? 1 : 0, false));
	}
	inline llvm::Constant* emitLiteral(V128 value)
	{
		return llvm::ConstantVector::get(
			{llvm::ConstantInt::get(llvmI128Type, llvm::APInt(128, 2, value.u64))});
	}
	inline llvm::Constant* emitLiteralPointer(const void* pointer, llvm::Type* type)
	{
		auto pointerInt = llvm::APInt(sizeof(Uptr) == 8 ? 64 : 32, reinterpret_cast<Uptr>(pointer));
		return llvm::Constant::getIntegerValue(type, pointerInt);
	}

	// Converts a WebAssembly type to a LLVM type.
	inline llvm::Type* asLLVMType(IR::ValueType type)
	{
		wavmAssert(type < IR::ValueType::num);
		return llvmValueTypes[Uptr(type)];
	}

	inline llvm::Type* asLLVMType(IR::TypeTuple typeTuple)
	{
		llvm::Type** llvmTypes = (llvm::Type**)alloca(sizeof(llvm::Type*) * typeTuple.size());
		for(Uptr typeIndex = 0; typeIndex < typeTuple.size(); ++typeIndex)
		{ llvmTypes[typeIndex] = asLLVMType(typeTuple[typeIndex]); }
		return llvm::StructType::get(*llvmContext,
									 llvm::ArrayRef<llvm::Type*>(llvmTypes, typeTuple.size()));
	}

	inline bool areResultsReturnedDirectly(IR::TypeTuple results)
	{
		// On X64, the calling conventions can return up to 3 i64s and 4 float/vectors.
		// For simplicity, just allow up to 3 values to be returned, including the implicitly
		// returned context pointer.
		enum
		{
			maxDirectlyReturnedValues = 3
		};
		return results.size() + 1 <= maxDirectlyReturnedValues;
	}

	inline llvm::StructType* getLLVMReturnStructType(IR::TypeTuple results)
	{
		if(areResultsReturnedDirectly(results))
		{
			// A limited number of results can be packed into a struct and returned directly.
			return llvm::StructType::get(llvmI8PtrType, asLLVMType(results));
		}
		else
		{
			// If there are too many results to be returned directly, they will be returned in
			// the context arg/return memory block.
			return llvm::StructType::get(llvmI8PtrType);
		}
	}

	inline llvm::Constant* getZeroedLLVMReturnStruct(IR::TypeTuple resultType)
	{
		return llvm::Constant::getNullValue(getLLVMReturnStructType(resultType));
	}

	// Converts a WebAssembly function type to a LLVM type.
	inline llvm::FunctionType* asLLVMType(IR::FunctionType functionType,
										  Runtime::CallingConvention callingConvention)
	{
		const Uptr numImplicitParameters
			= callingConvention == Runtime::CallingConvention::intrinsicWithMemAndTable
				  ? 3
				  : callingConvention == Runtime::CallingConvention::c ? 0 : 1;
		const Uptr numParameters = numImplicitParameters + functionType.params().size();
		auto llvmArgTypes        = (llvm::Type**)alloca(sizeof(llvm::Type*) * numParameters);
		if(callingConvention == Runtime::CallingConvention::intrinsicWithMemAndTable)
		{
			llvmArgTypes[0] = llvmI8PtrType;
			llvmArgTypes[1] = llvmI64Type;
			llvmArgTypes[2] = llvmI64Type;
		}
		else if(callingConvention != Runtime::CallingConvention::c)
		{
			llvmArgTypes[0] = llvmI8PtrType;
		}
		for(Uptr argIndex = 0; argIndex < functionType.params().size(); ++argIndex)
		{
			llvmArgTypes[argIndex + numImplicitParameters]
				= asLLVMType(functionType.params()[argIndex]);
		}

		llvm::Type* llvmReturnType;
		switch(callingConvention)
		{
		case Runtime::CallingConvention::wasm:
			llvmReturnType = getLLVMReturnStructType(functionType.results());
			break;

		case Runtime::CallingConvention::intrinsicWithContextSwitch:
			llvmReturnType = llvmI8PtrType;
			break;

		case Runtime::CallingConvention::intrinsicWithMemAndTable:
		case Runtime::CallingConvention::intrinsic:
		case Runtime::CallingConvention::c:
			switch(functionType.results().size())
			{
			case 0: llvmReturnType = llvmVoidType; break;
			case 1: llvmReturnType = asLLVMType(functionType.results()[0]); break;
			default: Errors::fatal("intrinsics/C functions returning >1 result isn't supported");
			}
			break;

		default: Errors::unreachable();
		};
		return llvm::FunctionType::get(
			llvmReturnType, llvm::ArrayRef<llvm::Type*>(llvmArgTypes, numParameters), false);
	}

	inline llvm::CallingConv::ID asLLVMCallingConv(Runtime::CallingConvention callingConvention)
	{
		switch(callingConvention)
		{
		case Runtime::CallingConvention::wasm: return llvm::CallingConv::Fast;

		case Runtime::CallingConvention::intrinsic:
		case Runtime::CallingConvention::intrinsicWithContextSwitch:
		case Runtime::CallingConvention::intrinsicWithMemAndTable:
		case Runtime::CallingConvention::c: return llvm::CallingConv::C;

		default: Errors::unreachable();
		}
	}

	inline llvm::Constant* getMemoryIdFromOffset(llvm::Constant* memoryOffset)
	{
		return llvm::ConstantExpr::getExactUDiv(
			llvm::ConstantExpr::getSub(
				memoryOffset, emitLiteral(offsetof(Runtime::CompartmentRuntimeData, memoryBases))),
			emitLiteral(sizeof(Uptr)));
	}

	inline llvm::Constant* getTableIdFromOffset(llvm::Constant* tableOffset)
	{
		return llvm::ConstantExpr::getExactUDiv(
			llvm::ConstantExpr::getSub(
				tableOffset, emitLiteral(offsetof(Runtime::CompartmentRuntimeData, tableBases))),
			emitLiteral(sizeof(Uptr)));
	}

	// Functions that map between the symbols used for externally visible functions and the function
	std::string getExternalFunctionName(Uptr functionDefIndex);
	bool getFunctionIndexFromExternalName(const char* externalName, Uptr& outFunctionDefIndex);

	// Emits LLVM IR for a module.
	void emitModule(const IR::Module& irModule, llvm::Module& outLLVMModule);

	// Used to override LLVM's default behavior of looking up unresolved symbols in DLL exports.
	llvm::JITEvaluatedSymbol resolveJITImport(llvm::StringRef name);

	// Information about a JIT symbol, used to map instruction pointers to descriptive names.
	struct JITSymbol
	{
		enum class Type
		{
			functionInstance,
			invokeThunk
		};
		Type type;
		union
		{
			Runtime::FunctionInstance* functionInstance;
			IR::FunctionType invokeThunkType;
		};
		Uptr baseAddress;
		Uptr numBytes;
		std::map<U32, U32> offsetToOpIndexMap;

		JITSymbol(Runtime::FunctionInstance* inFunctionInstance,
				  Uptr inBaseAddress,
				  Uptr inNumBytes,
				  std::map<U32, U32>&& inOffsetToOpIndexMap)
		: type(Type::functionInstance)
		, functionInstance(inFunctionInstance)
		, baseAddress(inBaseAddress)
		, numBytes(inNumBytes)
		, offsetToOpIndexMap(inOffsetToOpIndexMap)
		{
		}

		JITSymbol(IR::FunctionType inInvokeThunkType,
				  Uptr inBaseAddress,
				  Uptr inNumBytes,
				  std::map<U32, U32>&& inOffsetToOpIndexMap)
		: type(Type::invokeThunk)
		, invokeThunkType(inInvokeThunkType)
		, baseAddress(inBaseAddress)
		, numBytes(inNumBytes)
		, offsetToOpIndexMap(inOffsetToOpIndexMap)
		{
		}
	};

	struct UnitMemoryManager;

	// A unit of JIT compilation. Encapsulates the LLVM JIT compilation pipeline but allows
	// subclasses to define how the resulting code is used.
	struct JITUnit
	{
		JITUnit();
		~JITUnit();

		void load(const std::vector<U8>& objectBytes,
				  const HashMap<std::string, Uptr>& importedSymbolMap,
				  bool shouldLogMetrics);

		virtual JITSymbol* notifySymbolLoaded(const char* name,
											  Uptr baseAddress,
											  Uptr numBytes,
											  std::map<U32, U32>&& offsetToOpIndexMap)
			= 0;

	private:
		UnitMemoryManager* memoryManager;
		std::vector<JITSymbol*> symbols;
	};

	extern std::vector<U8> compileLLVMModule(llvm::Module&& llvmModule, bool shouldLogMetrics);

	extern void processSEHTables(U8* imageBase,
								 const llvm::LoadedObjectInfo& loadedObject,
								 const llvm::object::SectionRef& pdataSection,
								 const U8* pdataCopy,
								 Uptr pdataNumBytes,
								 const llvm::object::SectionRef& xdataSection,
								 const U8* xdataCopy,
								 Uptr sehTrampolineAddress);
}
