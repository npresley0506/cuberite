
// ManualBindings_World.cpp

// Implements the manual Lua API bindings for the cWorld class

#include "Globals.h"
#include "tolua++/include/tolua++.h"
#include "../World.h"
#include "../UUID.h"
#include "ManualBindings.h"
#include "LuaState.h"
#include "PluginLua.h"
#include "LuaChunkStay.h"

#include "BlockEntities/BeaconEntity.h"
#include "BlockEntities/BedEntity.h"
#include "BlockEntities/BrewingstandEntity.h"
#include "BlockEntities/ChestEntity.h"
#include "BlockEntities/CommandBlockEntity.h"
#include "BlockEntities/DispenserEntity.h"
#include "BlockEntities/DropSpenserEntity.h"
#include "BlockEntities/DropperEntity.h"
#include "BlockEntities/FlowerPotEntity.h"
#include "BlockEntities/FurnaceEntity.h"
#include "BlockEntities/HopperEntity.h"
#include "BlockEntities/MobHeadEntity.h"
#include "BlockEntities/NoteEntity.h"





/** Check that a Lua parameter is either a vector or 3 numbers in sequence
\param L The Lua state
\param a_VectorName name of the vector class e.g. "Vector3<int>"
\param a_Index Index to the start of the vector in the parameter list
\param[out] a_NextIndex Index of the next parameter after the vector
\retval true if the parameter is a vector or 3 numbers */
static bool CheckParamVectorOr3Numbers(cLuaState & L, const char * a_VectorName, int a_Index, int & a_NextIndex)
{
	if (L.IsParamUserType(a_Index, a_VectorName))
	{
		a_NextIndex = a_Index + 1;
		return L.CheckParamUserType(a_Index, a_VectorName);
	}

	a_NextIndex = a_Index + 3;
	return L.CheckParamNumber(a_Index, a_Index + 2);
}





/** Get a vector from the stack, which may be represented in lua as either a `Vector3<T>` or 3 numbers */
template <typename T>
static bool GetStackVectorOr3Numbers(cLuaState & L, int a_Index, Vector3<T> & a_Return)
{
	if (L.GetStackValue(a_Index, a_Return))
	{
		return true;
	}
	return L.GetStackValues(a_Index, a_Return.x, a_Return.y, a_Return.z);
}





/** Template for the bindings for the DoWithXYZAt(X, Y, Z) functions that don't need to check their coords. */
template <class BlockEntityType, BLOCKTYPE... BlockTypes>
static int DoWithBlockEntityAt(lua_State * tolua_S)
{
	cLuaState L(tolua_S);
	int OffsetIndex;

	// Check params:
	if (
		!L.CheckParamSelf("cWorld") ||
		!CheckParamVectorOr3Numbers(L, "Vector3<int>", 2, OffsetIndex) ||
		!L.CheckParamFunction(OffsetIndex) ||
		!L.CheckParamEnd(OffsetIndex + 1)
	)
	{
		return 0;
	}

	cWorld * Self = nullptr;
	Vector3i Position;
	cLuaState::cRef FnRef;

	// Get parameters:
	if (
		!L.GetStackValues(1, Self) ||
		!GetStackVectorOr3Numbers(L, 2, Position) ||
		!L.GetStackValues(OffsetIndex, FnRef)
	)
	{
		return 0;
	}

	if (Self == nullptr)
	{
		return L.ApiParamError("Invalid 'self'");
	}
	if (!FnRef.IsValid())
	{
		return L.ApiParamError("Expected a valid callback function for parameter %i", OffsetIndex);
	}

	// Call the DoWith function:
	bool res = Self->DoWithBlockEntityAt(Position, [&L, &FnRef](cBlockEntity & a_BlockEntity)
	{
		if constexpr (sizeof...(BlockTypes) != 0)
		{
			if (((a_BlockEntity.GetBlockType() != BlockTypes) && ...))
			{
				return false;
			}
		}

		bool ret = false;
		L.Call(FnRef, static_cast<BlockEntityType *>(&a_BlockEntity), cLuaState::Return, ret);
		return ret;
	});

	// Push the result as the return value:
	L.Push(res);
	return 1;
}





template <
	class Ty1,
	class Ty2,
	bool (Ty1::*ForEachFn)(const cBoundingBox &, cFunctionRef<bool(Ty2 &)>)
>
static int ForEachInBox(lua_State * tolua_S)
{
	// Check params:
	cLuaState L(tolua_S);
	if (
		!L.CheckParamUserType(1, "cWorld") ||
		!L.CheckParamUserType(2, "cBoundingBox") ||
		!L.CheckParamFunction(3) ||
		!L.CheckParamEnd(4)
	)
	{
		return 0;
	}

	// Get the params:
	Ty1 * Self = nullptr;
	cBoundingBox * Box = nullptr;
	cLuaState::cRef FnRef;
	L.GetStackValues(1, Self, Box, FnRef);
	if ((Self == nullptr) || (Box == nullptr))
	{
		return L.ApiParamError("Invalid world (%p) or boundingbox (%p)", static_cast<void *>(Self), static_cast<void *>(Box));
	}
	if (!FnRef.IsValid())
	{
		return L.ApiParamError("Expected a valid callback function for parameter #2");
	}

	bool res = (Self->*ForEachFn)(*Box, [&](Ty2 & a_Item)
		{
			bool ret = false;
			if (!L.Call(FnRef, &a_Item, cLuaState::Return, ret))
			{
				LOGWARNING("Failed to call Lua callback");
				L.LogStackTrace();
				return true;  // Abort enumeration
			}

			return ret;
		}
	);

	// Push the result as the return value:
	L.Push(res);
	return 1;
}





template <class BlockEntityType, BLOCKTYPE... BlockTypes>
static int ForEachBlockEntityInChunk(lua_State * tolua_S)
{
	// Check params:
	cLuaState L(tolua_S);
	if (
		!L.CheckParamSelf("cWorld") ||
		!L.CheckParamNumber(2, 3) ||
		!L.CheckParamFunction(4) ||
		!L.CheckParamEnd(5)
	)
	{
		return 0;
	}

	// Get parameters:
	cWorld * Self = nullptr;
	int ChunkX = 0;
	int ChunkZ = 0;
	cLuaState::cRef FnRef;
	L.GetStackValues(1, Self, ChunkX, ChunkZ, FnRef);
	if (Self == nullptr)
	{
		return L.ApiParamError("Error in function call '#funcname#': Invalid 'self'");
	}
	if (!FnRef.IsValid())
	{
		return L.ApiParamError("Expected a valid callback function for parameter #4");
	}

	// Call the ForEach function:
	bool res = Self->ForEachBlockEntityInChunk(ChunkX, ChunkZ, [&L, &FnRef](cBlockEntity & a_BlockEntity)
	{
		if constexpr (sizeof...(BlockTypes) != 0)
		{
			if (((a_BlockEntity.GetBlockType() != BlockTypes) && ...))
			{
				return false;
			}
		}

		bool ret = false;
		L.Call(FnRef, static_cast<BlockEntityType *>(&a_BlockEntity), cLuaState::Return, ret);
		return ret;
	});

	// Push the result as the return value:
	L.Push(res);
	return 1;
}





static int tolua_cWorld_BroadcastBlockAction(lua_State * tolua_S)
{
	/* Function signature:
	void BroadcastBlockAction(number a_BlockX, number a_BlockY, number a_BlockZ, number a_number1, number a_number2, number a_BlockType, cClientHandle a_Exclude)
	--or--
	void BroadcastBlockAction(Vector3<int> a_BlockPos, number a_Byte1, number a_Byte2, number a_BlockType, cClientHandle a_Exclude)
	*/

	cLuaState L(tolua_S);
	int Byte1Index;
	if (
		!L.CheckParamSelf("cWorld") ||
		!CheckParamVectorOr3Numbers(L, "Vector3<int>", 2, Byte1Index) ||
		!L.CheckParamNumber(Byte1Index, Byte1Index + 2)
	)
	{
		return 0;
	}

	if (Byte1Index != 3)  // Not the vector overload
	{
		L.LogStackTrace();
		LOGWARN("BroadcastBlockAction with 3 position arguments is deprecated, use vector-parametered version instead.");
	}

	// Read the params:
	cWorld * Self;
	Vector3i BlockPos;
	Byte Byte1, Byte2;
	BLOCKTYPE BlockType;
	const cClientHandle * Exclude = nullptr;

	if (
		!L.GetStackValues(1, Self) ||
		!GetStackVectorOr3Numbers(L, 2, BlockPos) ||
		!L.GetStackValues(Byte1Index, Byte1, Byte2, BlockType)
	)
	{
		return 0;
	}

	// Optional param
	L.GetStackValue(Byte1Index + 3, Exclude);

	Self->BroadcastBlockAction(BlockPos, Byte1, Byte2, BlockType, Exclude);
	return 0;
}





static int tolua_cWorld_BroadcastSoundEffect(lua_State * tolua_S)
{
	/* Function signature:
	void BroadcastSoundEffect(string a_SoundName, number a_X, number a_Y, number a_Z, number a_Volume, number a_Pitch, [cClientHandle * a_Exclude])
	--or--
	void BroadcastSoundEffect(string a_SoundName, Vector3d, number a_Volume, number a_Pitch, [cClientHandle a_Exclude])
	*/
	cLuaState L(tolua_S);
	int VolumeIndex;
	if (
		!L.CheckParamSelf("cWorld") ||
		!L.CheckParamString(2) ||
		!CheckParamVectorOr3Numbers(L, "Vector3<double>", 3, VolumeIndex) ||
		!L.CheckParamNumber(VolumeIndex, VolumeIndex + 1)
	)
	{
		return 0;
	}

	if (VolumeIndex != 4)  // Not the vector overload
	{
		L.LogStackTrace();
		LOGWARN("BroadcastSoundEffect with 3 position arguments is deprecated, use vector-parametered version instead.");
	}

	// Read the params:
	cWorld * Self;
	AString SoundName;
	Vector3d BlockPos;
	float Volume, Pitch;
	const cClientHandle * Exclude = nullptr;

	if (
		!L.GetStackValues(1, Self, SoundName) ||
		!GetStackVectorOr3Numbers(L, 3, BlockPos) ||
		!L.GetStackValues(VolumeIndex, Volume, Pitch)
	)
	{
		return 0;
	}

	// Optional param
	L.GetStackValue(VolumeIndex + 2, Exclude);

	Self->BroadcastSoundEffect(SoundName, BlockPos, Volume, Pitch, Exclude);
	return 0;
}





static int tolua_cWorld_BroadcastSoundParticleEffect(lua_State * tolua_S)
{
	/* Function signature:
	World:BroadcastSoundParticleEffect(EffectID a_EffectID, Vector3i a_SrcPos, number a_Data, [cClientHandle a_Exclude])
	--or--
	void BroadcastSoundParticleEffect(EffectID a_EffectID, number a_SrcX, number a_SrcY, number a_SrcZ, number a_Data, [cClientHandle a_Exclude])
	*/
	cLuaState L(tolua_S);
	int DataIndex;
	if (
		!L.CheckParamSelf("cWorld") ||
		!L.CheckParamNumber(2) ||
		!CheckParamVectorOr3Numbers(L, "Vector3<int>", 3, DataIndex) ||
		!L.CheckParamNumber(DataIndex)
	)
	{
		return 0;
	}

	if (DataIndex != 4)  // Not the vector overload
	{
		L.LogStackTrace();
		LOGWARN("BroadcastSoundParticleEffect with 3 position arguments is deprecated, use vector-parametered version instead.");
	}

	// Read the params:
	cWorld * World = nullptr;
	Int32 EffectId;
	Vector3i SrcPos;
	int Data;
	cClientHandle * ExcludeClient = nullptr;

	if (
		!L.GetStackValues(1, World, EffectId) ||
		!GetStackVectorOr3Numbers(L, 3, SrcPos) ||
		!L.GetStackValue(DataIndex, Data)
	)
	{
		return 0;
	}

	// Optional param
	L.GetStackValue(DataIndex + 1, ExcludeClient);

	World->BroadcastSoundParticleEffect(static_cast<EffectID>(EffectId), SrcPos, Data, ExcludeClient);
	return 0;
}





static int tolua_cWorld_BroadcastParticleEffect(lua_State * tolua_S)
{
	/* Function signature:
	World:BroadcastParticleEffect("Name", PosX, PosY, PosZ, OffX, OffY, OffZ, ParticleData, ParticleAmount, [ExcludeClient], [OptionalParam1], [OptionalParam2])
	--or--
	World:BroadcastParticleEffect("Name", SrcPos, Offset, ParticleData, ParticleAmount, [ExcludeClient], [OptionalParam1], [OptionalParam2])
	*/
	cLuaState L(tolua_S);
	int OffsetIndex, ParticleDataIndex;
	if (
		!L.CheckParamSelf("cWorld") ||
		!L.CheckParamString(2) ||
		!CheckParamVectorOr3Numbers(L, "Vector3<float>", 3, OffsetIndex) ||
		!CheckParamVectorOr3Numbers(L, "Vector3<float>", OffsetIndex, ParticleDataIndex)
	)
	{
		return 0;
	}

	if ((OffsetIndex != 4) || (ParticleDataIndex != 5))  // Not the vector overload
	{
		L.LogStackTrace();
		LOGWARN("BroadcastParticleEffect with 3 position and 3 offset arguments is deprecated, use vector-parametered version instead.");
	}

	// Read the params:
	cWorld * World = nullptr;
	AString Name;
	Vector3f SrcPos, Offset;
	float ParticleData;
	int ParticleAmount;
	cClientHandle * ExcludeClient = nullptr;

	if (
		!L.GetStackValues(1, World, Name) ||
		!GetStackVectorOr3Numbers(L, 3, SrcPos) ||
		!GetStackVectorOr3Numbers(L, OffsetIndex, Offset) ||
		!L.GetStackValues(ParticleDataIndex, ParticleData, ParticleAmount)
	)
	{
		return 0;
	}

	// Read up to 3 more optional params:
	L.GetStackValue(ParticleDataIndex + 2, ExcludeClient);

	std::array<int, 2> Data;
	bool HasData = L.GetStackValues(ParticleDataIndex + 3, Data[0], Data[1]);

	if (HasData)
	{
		World->BroadcastParticleEffect(Name, SrcPos, Offset, ParticleData, ParticleAmount, Data, ExcludeClient);
	}
	else
	{
		World->BroadcastParticleEffect(Name, SrcPos, Offset, ParticleData, ParticleAmount, ExcludeClient);
	}
	return 0;
}





static int tolua_cWorld_ChunkStay(lua_State * tolua_S)
{
	/* Function signature:
	World:ChunkStay(ChunkCoordTable, OnChunkAvailable, OnAllChunksAvailable)
	ChunkCoordTable == { {Chunk1x, Chunk1z}, {Chunk2x, Chunk2z}, ... }
	*/

	cLuaState L(tolua_S);
	if (
		!L.CheckParamUserType     (1, "cWorld") ||
		!L.CheckParamTable        (2) ||
		!L.CheckParamFunctionOrNil(3, 4)
	)
	{
		return 0;
	}

	// Read the params:
	cWorld * world;
	cLuaState::cStackTablePtr chunkCoords;
	cLuaState::cOptionalCallbackPtr onChunkAvailable, onAllChunksAvailable;  // Callbacks may be unassigned at all - as a request to load / generate chunks
	if (!L.GetStackValues(1, world, chunkCoords, onChunkAvailable, onAllChunksAvailable))
	{
		LOGWARNING("cWorld:ChunkStay(): Cannot read parameters, bailing out.");
		L.LogStackTrace();
		L.LogStackValues("Values on the stack");
		return 0;
	}
	if (world == nullptr)
	{
		LOGWARNING("World:ChunkStay(): invalid world parameter");
		L.LogStackTrace();
		return 0;
	}
	ASSERT(chunkCoords != nullptr);  // If the table was invalid, GetStackValues() would have failed

	// Read the chunk coords:
	auto chunkStay = std::make_unique<cLuaChunkStay>();
	if (!chunkStay->AddChunks(*chunkCoords))
	{
		return 0;
	}

	// Activate the ChunkStay:
	chunkStay.release()->Enable(*world->GetChunkMap(), std::move(onChunkAvailable), std::move(onAllChunksAvailable));
	return 0;
}





static int tolua_cWorld_DoExplosionAt(lua_State * tolua_S)
{
	/* Function signature:
	World:DoExplosionAt(ExplosionSize, BlockX, BlockY, BlockZ, CanCauseFire, SourceType, [SourceData])
	*/

	cLuaState L(tolua_S);
	if (
		!L.CheckParamUserType     (1, "cWorld") ||
		!L.CheckParamNumber       (2, 5) ||
		!L.CheckParamBool         (6) ||
		!L.CheckParamNumber       (7) ||
		!L.CheckParamEnd          (9)
	)
	{
		return 0;
	}

	// Read the params:
	cWorld * World;
	double ExplosionSize;
	int BlockX, BlockY, BlockZ;
	bool CanCauseFire;
	int SourceTypeInt;
	if (!L.GetStackValues(1, World, ExplosionSize, BlockX, BlockY, BlockZ, CanCauseFire, SourceTypeInt))
	{
		LOGWARNING("World:DoExplosionAt(): invalid parameters");
		L.LogStackTrace();
		return 0;
	}
	if ((SourceTypeInt < 0) || (SourceTypeInt >= esMax))
	{
		LOGWARNING("World:DoExplosionAt(): Invalid source type");
		L.LogStackTrace();
		return 0;
	}
	eExplosionSource SourceType;
	void * SourceData;
	switch (SourceTypeInt)
	{
		case esBed:
		{
			// esBed receives a Vector3i SourceData param:
			Vector3i pos;
			L.GetStackValue(8, pos);
			SourceType = esBed;
			SourceData = &pos;
			break;
		}

		case esEnderCrystal:
		case esGhastFireball:
		case esMonster:
		case esPrimedTNT:
		case esWitherBirth:
		case esWitherSkull:
		{
			// These all receive a cEntity descendant SourceData param:
			cEntity * ent = nullptr;
			L.GetStackValue(8, ent);
			SourceType = static_cast<eExplosionSource>(SourceTypeInt);
			SourceData = ent;
			break;
		}

		case esOther:
		case esPlugin:
		{
			// esOther and esPlugin ignore their SourceData params
			SourceType = static_cast<eExplosionSource>(SourceTypeInt);
			SourceData = nullptr;
			break;
		}

		default:
		{
			LOGWARNING("cWorld:DoExplosionAt(): invalid SourceType parameter: %d", SourceTypeInt);
			L.LogStackTrace();
			return 0;
		}
	}

	// Create the actual explosion:
	World->DoExplosionAt(ExplosionSize, BlockX, BlockY, BlockZ, CanCauseFire, SourceType, SourceData);

	return 0;
}





static int tolua_cWorld_DoWithPlayerByUUID(lua_State * tolua_S)
{
	// Check params:
	cLuaState L(tolua_S);
	if (
		!L.CheckParamSelf("cWorld") ||
		!L.CheckParamUUID(2) ||
		!L.CheckParamFunction(3) ||
		!L.CheckParamEnd(4)
	)
	{
		return 0;
	}

	// Get parameters:
	cWorld * Self;
	cUUID PlayerUUID;
	cLuaState::cRef FnRef;
	L.GetStackValues(1, Self, PlayerUUID, FnRef);

	if (PlayerUUID.IsNil())
	{
		return L.ApiParamError("Expected a non-nil UUID for parameter #1");
	}
	if (!FnRef.IsValid())
	{
		return L.ApiParamError("Expected a valid callback function for parameter #2");
	}

	// Call the function:
	bool res = Self->DoWithPlayerByUUID(PlayerUUID, [&](cPlayer & a_Player)
		{
			bool ret = false;
			L.Call(FnRef, &a_Player, cLuaState::Return, ret);
			return ret;
		}
	);

	// Push the result as the return value:
	L.Push(res);
	return 1;
}





static int tolua_cWorld_DoWithNearestPlayer(lua_State * tolua_S)
{
	// Check params:
	cLuaState L(tolua_S);
	if (
		!L.CheckParamSelf("cWorld") ||
		!L.CheckParamUserType(2, "Vector3<double>") ||
		!L.CheckParamNumber(3) ||
		!L.CheckParamFunction(4) ||
		// Params 5 and 6 are optional bools, no check for those
		!L.CheckParamEnd(7)
	)
	{
		return 0;
	}

	// Get parameters:
	cWorld * Self;
	Vector3d Position;
	double RangeLimit;
	cLuaState::cRef FnRef;
	bool CheckLineOfSight = true, IgnoreSpectators = true;  // Defaults for the optional params
	L.GetStackValues(1, Self, Position, RangeLimit, FnRef, CheckLineOfSight, IgnoreSpectators);

	if (!FnRef.IsValid())
	{
		return L.ApiParamError("Expected a valid callback function for parameter #3");
	}

	// Call the function:
	bool res = Self->DoWithNearestPlayer(Position, RangeLimit, [&](cPlayer & a_Player)
	{
		bool ret = false;
		L.Call(FnRef, &a_Player, cLuaState::Return, ret);
		return ret;
	}, CheckLineOfSight, IgnoreSpectators);

	// Push the result as the return value:
	L.Push(res);
	return 1;
}





static int tolua_cWorld_FastSetBlock(lua_State * tolua_S)
{
	/* Function signature:
	World:FastSetBlock(BlockX, BlockY, BlockZ)
	--or--
	World:FastSetBlock(Position)
	*/

	cLuaState L(tolua_S);
	int OffsetIndex;
	if (
		!L.CheckParamSelf("cWorld") ||
		!CheckParamVectorOr3Numbers(L, "Vector3<int>", 2, OffsetIndex) ||
		!L.CheckParamNumber(OffsetIndex, OffsetIndex + 1) ||
		!L.CheckParamEnd(OffsetIndex + 2)
	)
	{
		return 0;
	}

	if (OffsetIndex != 3)  // Not the vector overload
	{
		L.LogStackTrace();
		LOGWARN("GetBlockBlockLight with 3 position arguments is deprecated, use vector-parametered version instead.");
	}

	cWorld * World;
	Vector3i Position;
	BLOCKTYPE Type;
	NIBBLETYPE Meta;

	// Read the params:
	if (
		!L.GetStackValues(1, World) ||
		!GetStackVectorOr3Numbers(L, 2, Position) ||
		!L.GetStackValues(OffsetIndex, Type, Meta)
	)
	{
		return 0;
	}

	if (World == nullptr)
	{
		return cManualBindings::lua_do_error(tolua_S, "Error in function call '#funcname#': Invalid 'self'");
	}

	if (!cChunkDef::IsValidHeight(Position.y))
	{
		return cManualBindings::lua_do_error(tolua_S, "Error in function call '#funcname#': Invalid 'position'");
	}

	World->FastSetBlock(Position, Type, Meta);
	return 0;
}





static int tolua_cWorld_ForEachEntityInChunk(lua_State * tolua_S)
{
	// Check params:
	cLuaState L(tolua_S);
	if (
		!L.CheckParamUserType(1, "cWorld") ||
		!L.CheckParamNumber(2, 3) ||
		!L.CheckParamFunction(4) ||
		!L.CheckParamEnd(5)
	)
	{
		return 0;
	}

	// Get parameters:
	cWorld * Self = nullptr;
	int ChunkX = 0;
	int ChunkZ = 0;
	cLuaState::cRef FnRef;
	L.GetStackValues(1, Self, ChunkX, ChunkZ, FnRef);
	if (Self == nullptr)
	{
		return L.ApiParamError("Invalid 'self'");
	}
	if (!FnRef.IsValid())
	{
		return L.ApiParamError("Expected a valid callback function for parameter #4");
	}

	// Call the DoWith function:
	bool res = Self->ForEachEntityInChunk(ChunkX, ChunkZ, [&](cEntity & a_Item)
		{
			bool ret = false;
			L.Call(FnRef, &a_Item, cLuaState::Return, ret);
			return ret;
		}
	);

	// Push the result as the return value:
	L.Push(res);
	return 1;
}





static int tolua_cWorld_ForEachLoadedChunk(lua_State * tolua_S)
{
	// Exported manually, because tolua doesn't support converting functions to functor types.
	// Function signature: ForEachLoadedChunk(callback) -> bool

	cLuaState L(tolua_S);
	if (
		!L.CheckParamUserType(1, "cWorld") ||
		!L.CheckParamFunction(2)
	)
	{
		return 0;
	}

	cPluginLua * Plugin = cManualBindings::GetLuaPlugin(tolua_S);
	if (Plugin == nullptr)
	{
		return 0;
	}

	// Read the params:
	cWorld * World = static_cast<cWorld *>(tolua_tousertype(tolua_S, 1, nullptr));
	if (World == nullptr)
	{
		LOGWARNING("World:ForEachLoadedChunk(): invalid world parameter");
		L.LogStackTrace();
		return 0;
	}
	cLuaState::cRef FnRef;
	L.GetStackValues(2, FnRef);
	if (!FnRef.IsValid())
	{
		return cManualBindings::lua_do_error(tolua_S, "Error in function call '#funcname#': Could not get function reference of parameter #2");
	}

	// Call the enumeration:
	bool ret = World->ForEachLoadedChunk(
		[&L, &FnRef](int a_ChunkX, int a_ChunkZ) -> bool
		{
			bool res = false;  // By default continue the enumeration
			L.Call(FnRef, a_ChunkX, a_ChunkZ, cLuaState::Return, res);
			return res;
		}
	);

	// Push the return value:
	L.Push(ret);
	return 1;
}





static int tolua_cWorld_GetBlock(lua_State * tolua_S)
{
	/* Function signature:
	World:GetBlock(BlockX, BlockY, BlockZ) -> BlockType
	--or--
	World:GetBlock(Position) -> BlockType
	*/

	cLuaState L(tolua_S);
	int OffsetIndex;
	if (
		!L.CheckParamSelf("cWorld") ||
		!CheckParamVectorOr3Numbers(L, "Vector3<int>", 2, OffsetIndex) ||
		!L.CheckParamEnd(OffsetIndex)
	)
	{
		return 0;
	}

	if (OffsetIndex != 3)  // Not the vector overload
	{
		L.LogStackTrace();
		LOGWARN("GetBlock with 3 position arguments is deprecated, use vector-parametered version instead.");
	}

	cWorld * World;
	Vector3i Position;

	// Read the params:
	if (
		!L.GetStackValues(1, World) ||
		!GetStackVectorOr3Numbers(L, 2, Position)
	)
	{
		return 0;
	}

	if (World == nullptr)
	{
		return cManualBindings::lua_do_error(tolua_S, "Error in function call '#funcname#': Invalid 'self'");
	}

	if (!cChunkDef::IsValidHeight(Position.y))
	{
		L.Push(E_BLOCK_AIR);
		return 1;
	}

	L.Push(World->GetBlock(Position));
	return 1;
}





static int tolua_cWorld_GetBlockBlockLight(lua_State * tolua_S)
{
	/* Function signature:
	World:GetBlockBlockLight(BlockX, BlockY, BlockZ) -> BlockLight
	--or--
	World:GetBlockBlockLight(Position) -> BlockLight
	*/

	cLuaState L(tolua_S);
	int OffsetIndex;
	if (
		!L.CheckParamSelf("cWorld") ||
		!CheckParamVectorOr3Numbers(L, "Vector3<int>", 2, OffsetIndex) ||
		!L.CheckParamEnd(OffsetIndex)
	)
	{
		return 0;
	}

	if (OffsetIndex != 3)  // Not the vector overload
	{
		L.LogStackTrace();
		LOGWARN("GetBlockBlockLight with 3 position arguments is deprecated, use vector-parametered version instead.");
	}

	cWorld * World;
	Vector3i Position;

	// Read the params:
	if (
		!L.GetStackValues(1, World) ||
		!GetStackVectorOr3Numbers(L, 2, Position)
	)
	{
		return 0;
	}

	if (World == nullptr)
	{
		return cManualBindings::lua_do_error(tolua_S, "Error in function call '#funcname#': Invalid 'self'");
	}

	if (!cChunkDef::IsValidHeight(Position.y))
	{
		return cManualBindings::lua_do_error(tolua_S, "Error in function call '#funcname#': Invalid 'position'");
	}

	L.Push(World->GetBlockBlockLight(Position));
	return 1;
}





static int tolua_cWorld_GetBlockInfo(lua_State * tolua_S)
{
	/* Exported manually, because tolua would generate useless additional parameters (a_BlockType .. a_BlockSkyLight)
	Function signature:
	GetBlockInfo(BlockX, BlockY, BlockZ) -> BlockValid, [BlockType, BlockMeta, BlockSkyLight, BlockBlockLight]
	--or--
	GetBlockInfo(Position) -> BlockValid, [BlockType, BlockMeta, BlockSkyLight, BlockBlockLight]
	*/

	// Check params:
	cLuaState L(tolua_S);
	int OffsetIndex;
	if (
		!L.CheckParamSelf("cWorld") ||
		!CheckParamVectorOr3Numbers(L, "Vector3<int>", 2, OffsetIndex) ||
		!L.CheckParamEnd(OffsetIndex)
	)
	{
		return 0;
	}

	if (OffsetIndex != 3)  // Not the vector overload
	{
		L.LogStackTrace();
		LOGWARN("GetBlockInfo with 3 position arguments is deprecated, use vector-parametered version instead.");
	}

	cWorld * World;
	Vector3i Position;

	// Read the params:
	if (
		!L.GetStackValues(1, World) ||
		!GetStackVectorOr3Numbers(L, 2, Position)
	)
	{
		return 0;
	}

	if (World == nullptr)
	{
		return cManualBindings::lua_do_error(tolua_S, "Error in function call '#funcname#': Invalid 'self'");
	}

	if (!cChunkDef::IsValidHeight(Position.y))
	{
		return cManualBindings::lua_do_error(tolua_S, "Error in function call '#funcname#': Invalid 'position'");
	}

	BLOCKTYPE BlockType;
	NIBBLETYPE BlockMeta, BlockSkyLight, BlockBlockLight;

	// Call the function:
	bool res = World->GetBlockInfo(Position, BlockType, BlockMeta, BlockSkyLight, BlockBlockLight);

	// Push the returned values:
	L.Push(res);
	if (res)
	{
		L.Push(BlockType, BlockMeta, BlockSkyLight, BlockBlockLight);
		return 5;
	}
	return 1;
}





static int tolua_cWorld_GetBlockMeta(lua_State * tolua_S)
{
	/* Function signature:
	World:GetBlockMeta(BlockX, BlockY, BlockZ) -> BlockMeta
	--or--
	World:GetBlockMeta(Position) -> BlockMeta
	*/

	cLuaState L(tolua_S);
	int OffsetIndex;
	if (
		!L.CheckParamSelf("cWorld") ||
		!CheckParamVectorOr3Numbers(L, "Vector3<int>", 2, OffsetIndex) ||
		!L.CheckParamEnd(OffsetIndex)
	)
	{
		return 0;
	}

	if (OffsetIndex != 3)  // Not the vector overload
	{
		L.LogStackTrace();
		LOGWARN("GetBlockMeta with 3 position arguments is deprecated, use vector-parametered version instead.");
	}

	cWorld * World;
	Vector3i Position;

	// Read the params:
	if (
		!L.GetStackValues(1, World) ||
		!GetStackVectorOr3Numbers(L, 2, Position)
	)
	{
		return 0;
	}

	if (World == nullptr)
	{
		return cManualBindings::lua_do_error(tolua_S, "Error in function call '#funcname#': Invalid 'self'");
	}

	if (!cChunkDef::IsValidHeight(Position.y))
	{
		L.Push(0);
		return 1;
	}

	L.Push(World->GetBlockMeta(Position));
	return 1;
}





static int tolua_cWorld_GetBlockSkyLight(lua_State * tolua_S)
{
	/* Function signature:
	World:GetBlockSkyLight(BlockX, BlockY, BlockZ) -> BlockLight
	--or--
	World:GetBlockSkyLight(Position) -> BlockLight
	*/

	cLuaState L(tolua_S);
	int OffsetIndex;
	if (
		!L.CheckParamSelf("cWorld") ||
		!CheckParamVectorOr3Numbers(L, "Vector3<int>", 2, OffsetIndex) ||
		!L.CheckParamEnd(OffsetIndex)
	)
	{
		return 0;
	}

	if (OffsetIndex != 3)  // Not the vector overload
	{
		L.LogStackTrace();
		LOGWARN("GetBlockSkyLight with 3 position arguments is deprecated, use vector-parametered version instead.");
	}

	cWorld * World;
	Vector3i Position;

	// Read the params:
	if (
		!L.GetStackValues(1, World) ||
		!GetStackVectorOr3Numbers(L, 2, Position)
	)
	{
		return 0;
	}

	if (World == nullptr)
	{
		return cManualBindings::lua_do_error(tolua_S, "Error in function call '#funcname#': Invalid 'self'");
	}

	if (!cChunkDef::IsValidHeight(Position.y))
	{
		return cManualBindings::lua_do_error(tolua_S, "Error in function call '#funcname#': Invalid 'position'");
	}

	L.Push(World->GetBlockSkyLight(Position));
	return 1;
}





static int tolua_cWorld_GetBlockTypeMeta(lua_State * tolua_S)
{
	/* Exported manually, because tolua would generate useless additional parameters (a_BlockType, a_BlockMeta)
	Function signature:
	GetBlockTypeMeta(BlockX, BlockY, BlockZ) -> BlockValid, [BlockType, BlockMeta]
	--or--
	GetBlockTypeMeta(Position) -> BlockValid, [BlockType, BlockMeta]
	*/

	// Check params:
	cLuaState L(tolua_S);
	int OffsetIndex;
	if (
		!L.CheckParamSelf("cWorld") ||
		!CheckParamVectorOr3Numbers(L, "Vector3<int>", 2, OffsetIndex) ||
		!L.CheckParamEnd(OffsetIndex)
	)
	{
		return 0;
	}

	if (OffsetIndex != 3)  // Not the vector overload
	{
		L.LogStackTrace();
		LOGWARN("GetBlockTypeMeta with 3 position arguments is deprecated, use vector-parametered version instead.");
	}

	cWorld * World;
	Vector3i Position;

	// Read the params:
	if (
		!L.GetStackValues(1, World) ||
		!GetStackVectorOr3Numbers(L, 2, Position)
	)
	{
		return 0;
	}

	if (World == nullptr)
	{
		return cManualBindings::lua_do_error(tolua_S, "Error in function call '#funcname#': Invalid 'self'");
	}

	if (!cChunkDef::IsValidHeight(Position.y))
	{
		L.Push(E_BLOCK_AIR, 0);
		return 2;
	}

	BLOCKTYPE BlockType;
	NIBBLETYPE BlockMeta;

	// Call the function:
	bool res = World->GetBlockTypeMeta(Position, BlockType, BlockMeta);

	// Push the returned values:
	L.Push(res);
	if (res)
	{
		L.Push(BlockType, BlockMeta);
		return 3;
	}
	return 1;
}





static int tolua_cWorld_GetSignLines(lua_State * tolua_S)
{
	// Exported manually, because tolua would generate useless additional parameters (a_Line1 .. a_Line4)

	// Check params:
	cLuaState L(tolua_S);
	if (
		!L.CheckParamUserType(1, "cWorld") ||
		!L.CheckParamNumber(2, 4) ||
		!L.CheckParamEnd(5)
	)
	{
		return 0;
	}

	// Get params:
	cWorld * Self = nullptr;
	int BlockX = 0;
	int BlockY = 0;
	int BlockZ = 0;
	L.GetStackValues(1, Self, BlockX, BlockY, BlockZ);
	if (Self == nullptr)
	{
		return cManualBindings::lua_do_error(tolua_S, "Error in function call '#funcname#': Invalid 'self'");
	}

	// Call the function:
	AString Line1, Line2, Line3, Line4;
	bool res = Self->GetSignLines(BlockX, BlockY, BlockZ, Line1, Line2, Line3, Line4);

	// Push the returned values:
	L.Push(res);
	if (res)
	{
		L.Push(Line1, Line2, Line3, Line4);
		return 5;
	}
	return 1;
}





static int tolua_cWorld_GetTimeOfDay(lua_State * tolua_S)
{
	// Check params:
	cLuaState L(tolua_S);
	if (
		!L.CheckParamSelf("cWorld") ||
		!L.CheckParamEnd(2)
	)
	{
		return 0;
	}

	// Get params:
	cWorld * Self = nullptr;
	L.GetStackValues(1, Self);
	if (Self == nullptr)
	{
		return L.ApiParamError("Invalid 'self'");
	}

	// Call the function:
	const auto Time = Self->GetTimeOfDay();

	// Push the returned value:
	L.Push(Time.count());
	return 1;
}





static int tolua_cWorld_GetWorldAge(lua_State * tolua_S)
{
	// Check params:
	cLuaState L(tolua_S);
	if (
		!L.CheckParamSelf("cWorld") ||
		!L.CheckParamEnd(2)
	)
	{
		return 0;
	}

	// Get params:
	cWorld * Self = nullptr;
	L.GetStackValues(1, Self);
	if (Self == nullptr)
	{
		return L.ApiParamError("Invalid 'self'");
	}

	// Call the function:
	const auto Time = Self->GetWorldAge();

	// Push the returned value:
	L.Push(static_cast<lua_Number>(Time.count()));
	return 1;
}





static int tolua_cWorld_PrepareChunk(lua_State * tolua_S)
{
	/* Function signature:
	World:PrepareChunk(ChunkX, ChunkZ, Callback)
	*/

	// Check the param types:
	cLuaState L(tolua_S);
	if (
		!L.CheckParamUserType     (1, "cWorld") ||
		!L.CheckParamNumber       (2, 3) ||
		!L.CheckParamFunctionOrNil(4)
	)
	{
		return 0;
	}

	// Wrap the Lua callback inside a C++ callback class:
	class cCallback:
		public cChunkCoordCallback
	{
	public:
		// cChunkCoordCallback override:
		virtual void Call(cChunkCoords a_Coords, bool a_IsSuccess) override
		{
			m_LuaCallback.Call(a_Coords.m_ChunkX, a_Coords.m_ChunkZ, a_IsSuccess);
		}

		cLuaState::cOptionalCallback m_LuaCallback;
	};

	// Read the params:
	cWorld * world = nullptr;
	int chunkX = 0;
	int chunkZ = 0;
	auto Callback = std::make_unique<cCallback>();
	L.GetStackValues(1, world, chunkX, chunkZ, Callback->m_LuaCallback);
	if (world == nullptr)
	{
		LOGWARNING("World:PrepareChunk(): invalid world parameter");
		L.LogStackTrace();
		return 0;
	}

	// Call the chunk preparation:
	world->PrepareChunk(chunkX, chunkZ, std::move(Callback));
	return 0;
}





static int tolua_cWorld_QueueTask(lua_State * tolua_S)
{
	// Function signature:
	// World:QueueTask(Callback)

	// Retrieve the args:
	cLuaState L(tolua_S);
	if (
		!L.CheckParamUserType(1, "cWorld") ||
		!L.CheckParamFunction(2)
	)
	{
		return 0;
	}
	cWorld * World;
	cLuaState::cCallbackSharedPtr Task;
	if (!L.GetStackValues(1, World, Task))
	{
		return cManualBindings::lua_do_error(tolua_S, "Error in function call '#funcname#': Cannot read parameters");
	}
	if (World == nullptr)
	{
		return cManualBindings::lua_do_error(tolua_S, "Error in function call '#funcname#': Not called on an object instance");
	}
	if (!Task->IsValid())
	{
		return cManualBindings::lua_do_error(tolua_S, "Error in function call '#funcname#': Could not store the callback parameter");
	}

	World->QueueTask([Task](cWorld & a_World)
		{
			Task->Call(&a_World);
		}
	);
	return 0;
}





static int tolua_cWorld_SetBlock(lua_State * tolua_S)
{
	/* Function signature:
	World:SetBlock(BlockX, BlockY, BlockZ)
	--or--
	World:SetBlock(Position)
	*/

	cLuaState L(tolua_S);
	int OffsetIndex;
	if (
		!L.CheckParamSelf("cWorld") ||
		!CheckParamVectorOr3Numbers(L, "Vector3<int>", 2, OffsetIndex) ||
		!L.CheckParamNumber(OffsetIndex, OffsetIndex + 1) ||
		!L.CheckParamEnd(OffsetIndex + 2)
	)
	{
		return 0;
	}

	if (OffsetIndex != 3)  // Not the vector overload
	{
		L.LogStackTrace();
		LOGWARN("GetBlockBlockLight with 3 position arguments is deprecated, use vector-parametered version instead.");
	}

	cWorld * World;
	Vector3i Position;
	BLOCKTYPE Type;
	NIBBLETYPE Meta;

	// Read the params:
	if (
		!L.GetStackValues(1, World) ||
		!GetStackVectorOr3Numbers(L, 2, Position) ||
		!L.GetStackValues(OffsetIndex, Type, Meta)
	)
	{
		return 0;
	}

	if (World == nullptr)
	{
		return cManualBindings::lua_do_error(tolua_S, "Error in function call '#funcname#': Invalid 'self'");
	}

	if (!cChunkDef::IsValidHeight(Position.y))
	{
		return cManualBindings::lua_do_error(tolua_S, "Error in function call '#funcname#': Invalid 'position'");
	}

	World->SetBlock(Position, Type, Meta);
	return 0;
}





static int tolua_cWorld_SetSignLines(lua_State * tolua_S)
{
	// Exported manually, because tolua would generate useless additional return values (a_Line1 .. a_Line4)

	// Check params:
	cLuaState L(tolua_S);
	if (
		!L.CheckParamUserType(1, "cWorld") ||
		!L.CheckParamNumber(2, 4) ||
		!L.CheckParamString(5, 8) ||
		!L.CheckParamEnd(9)
	)
	{
		return 0;
	}

	// Get params:
	cWorld * Self = nullptr;
	int BlockX = 0;
	int BlockY = 0;
	int BlockZ = 0;
	AString Line1, Line2, Line3, Line4;
	L.GetStackValues(1, Self, BlockX, BlockY, BlockZ, Line1, Line2, Line3, Line4);
	if (Self == nullptr)
	{
		return cManualBindings::lua_do_error(tolua_S, "Error in function call '#funcname#': Invalid 'self'");
	}

	// Call the function:
	bool res = Self->SetSignLines(BlockX, BlockY, BlockZ, Line1, Line2, Line3, Line4);

	// Push the returned values:
	L.Push(res);
	return 1;
}





static int tolua_cWorld_SetTimeOfDay(lua_State * tolua_S)
{
	// Check params:
	cLuaState L(tolua_S);
	if (
		!L.CheckParamSelf("cWorld") ||
		!L.CheckParamNumber(2) ||
		!L.CheckParamEnd(3)
	)
	{
		return 0;
	}

	// Get params:
	cWorld * Self = nullptr;
	cTickTime::rep Time;
	L.GetStackValues(1, Self, Time);
	if (Self == nullptr)
	{
		return L.ApiParamError("Invalid 'self'");
	}

	// Call the function:
	Self->SetTimeOfDay(cTickTime(Time));
	return 0;
}





static int tolua_cWorld_ScheduleTask(lua_State * tolua_S)
{
	// Function signature:
	// World:ScheduleTask(NumTicks, Callback)

	// Retrieve the args:
	cLuaState L(tolua_S);
	if (
		!L.CheckParamUserType(1, "cWorld") ||
		!L.CheckParamNumber  (2) ||
		!L.CheckParamFunction(3)
	)
	{
		return 0;
	}
	cWorld * World;
	int NumTicks;
	auto Task = std::make_shared<cLuaState::cCallback>();
	if (!L.GetStackValues(1, World, NumTicks, Task))
	{
		return cManualBindings::lua_do_error(tolua_S, "Error in function call '#funcname#': Cannot read parameters");
	}
	if (World == nullptr)
	{
		return cManualBindings::lua_do_error(tolua_S, "Error in function call '#funcname#': Not called on an object instance");
	}
	if (!Task->IsValid())
	{
		return cManualBindings::lua_do_error(tolua_S, "Error in function call '#funcname#': Could not store the callback parameter");
	}

	World->ScheduleTask(cTickTime(NumTicks), [Task](cWorld & a_World)
		{
			Task->Call(&a_World);
		}
	);
	return 0;
}





static int tolua_cWorld_SpawnSplitExperienceOrbs(lua_State* tolua_S)
{
	cLuaState L(tolua_S);
	if (
		!L.CheckParamSelf("cWorld") ||
		!L.CheckParamUserType(2, "Vector3<double>") ||
		!L.CheckParamNumber(3) ||
		!L.CheckParamEnd(4)
	)
	{
		return 0;
	}

	cWorld * self = nullptr;
	Vector3d Position;
	int Reward;
	L.GetStackValues(1, self, Position, Reward);
	if (self == nullptr)
	{
		tolua_error(tolua_S, "Invalid 'self' in function 'SpawnSplitExperienceOrbs'", nullptr);
		return 0;
	}

	// Execute and push result:
	L.Push(self->SpawnExperienceOrb(Position, Reward));
	return 1;
}





static int tolua_cWorld_TryGetHeight(lua_State * tolua_S)
{
	/* Exported manually, because tolua would require the out-only param a_Height to be used when calling
	Function signature: world:TryGetHeight(a_World, a_BlockX, a_BlockZ) -> IsValid, Height
	*/

	// Check params:
	cLuaState L(tolua_S);
	if (
		!L.CheckParamUserType(1, "cWorld") ||
		!L.CheckParamNumber(2, 3) ||
		!L.CheckParamEnd(4)
	)
	{
		return 0;
	}

	// Get params:
	cWorld * self = nullptr;
	int BlockX = 0;
	int BlockZ = 0;
	L.GetStackValues(1, self, BlockX, BlockZ);
	if (self == nullptr)
	{
		tolua_error(tolua_S, "Invalid 'self' in function 'TryGetHeight'", nullptr);
		return 0;
	}

	// Call the implementation:
	int Height = 0;
	bool res = self->TryGetHeight(BlockX, BlockZ, Height);
	L.Push(res);
	if (res)
	{
		L.Push(Height);
		return 2;
	}
	return 1;
}





void cManualBindings::BindWorld(lua_State * tolua_S)
{
	tolua_beginmodule(tolua_S, nullptr);
		tolua_beginmodule(tolua_S, "cWorld");
			tolua_function(tolua_S, "BroadcastBlockAction",         tolua_cWorld_BroadcastBlockAction);
			tolua_function(tolua_S, "BroadcastSoundEffect",         tolua_cWorld_BroadcastSoundEffect);
			tolua_function(tolua_S, "BroadcastSoundParticleEffect", tolua_cWorld_BroadcastSoundParticleEffect);
			tolua_function(tolua_S, "BroadcastParticleEffect",      tolua_cWorld_BroadcastParticleEffect);
			tolua_function(tolua_S, "ChunkStay",                    tolua_cWorld_ChunkStay);
			tolua_function(tolua_S, "DoExplosionAt",                tolua_cWorld_DoExplosionAt);
			tolua_function(tolua_S, "DoWithBeaconAt",               DoWithBlockEntityAt<cBeaconEntity, E_BLOCK_BEACON>);
			tolua_function(tolua_S, "DoWithBedAt",                  DoWithBlockEntityAt<cBedEntity, E_BLOCK_BED>);
			tolua_function(tolua_S, "DoWithBlockEntityAt",          DoWithBlockEntityAt<cBlockEntity>);
			tolua_function(tolua_S, "DoWithBrewingstandAt",         DoWithBlockEntityAt<cBrewingstandEntity, E_BLOCK_BREWING_STAND>);
			tolua_function(tolua_S, "DoWithChestAt",                DoWithBlockEntityAt<cChestEntity, E_BLOCK_CHEST, E_BLOCK_TRAPPED_CHEST>);
			tolua_function(tolua_S, "DoWithCommandBlockAt",         DoWithBlockEntityAt<cCommandBlockEntity, E_BLOCK_COMMAND_BLOCK>);
			tolua_function(tolua_S, "DoWithDispenserAt",            DoWithBlockEntityAt<cDispenserEntity, E_BLOCK_DISPENSER>);
			tolua_function(tolua_S, "DoWithDropSpenserAt",          DoWithBlockEntityAt<cDropSpenserEntity, E_BLOCK_DISPENSER, E_BLOCK_DROPPER>);
			tolua_function(tolua_S, "DoWithDropperAt",              DoWithBlockEntityAt<cDropperEntity, E_BLOCK_DROPPER>);
			tolua_function(tolua_S, "DoWithEntityByID",             DoWithID<cWorld, cEntity, &cWorld::DoWithEntityByID>);
			tolua_function(tolua_S, "DoWithFlowerPotAt",            DoWithBlockEntityAt<cFlowerPotEntity, E_BLOCK_FLOWER_POT>);
			tolua_function(tolua_S, "DoWithFurnaceAt",              DoWithBlockEntityAt<cFurnaceEntity, E_BLOCK_FURNACE, E_BLOCK_LIT_FURNACE>);
			tolua_function(tolua_S, "DoWithHopperAt",               DoWithBlockEntityAt<cHopperEntity, E_BLOCK_HOPPER>);
			tolua_function(tolua_S, "DoWithMobHeadAt",              DoWithBlockEntityAt<cMobHeadEntity, E_BLOCK_HEAD>);
			tolua_function(tolua_S, "DoWithNearestPlayer",          tolua_cWorld_DoWithNearestPlayer);
			tolua_function(tolua_S, "DoWithNoteBlockAt",            DoWithBlockEntityAt<cNoteEntity, E_BLOCK_NOTE_BLOCK>);
			tolua_function(tolua_S, "DoWithPlayer",                 DoWith<cWorld, cPlayer, &cWorld::DoWithPlayer>);
			tolua_function(tolua_S, "DoWithPlayerByUUID",           tolua_cWorld_DoWithPlayerByUUID);
			tolua_function(tolua_S, "FastSetBlock",                 tolua_cWorld_FastSetBlock);
			tolua_function(tolua_S, "FindAndDoWithPlayer",          DoWith<cWorld, cPlayer, &cWorld::FindAndDoWithPlayer>);
			tolua_function(tolua_S, "ForEachBlockEntityInChunk",    ForEachBlockEntityInChunk<cBlockEntity>);
			tolua_function(tolua_S, "ForEachBrewingstandInChunk",   ForEachBlockEntityInChunk<cBrewingstandEntity, E_BLOCK_BREWING_STAND>);
			tolua_function(tolua_S, "ForEachChestInChunk",          ForEachBlockEntityInChunk<cChestEntity, E_BLOCK_CHEST, E_BLOCK_TRAPPED_CHEST>);
			tolua_function(tolua_S, "ForEachEntity",                ForEach<cWorld, cEntity, &cWorld::ForEachEntity>);
			tolua_function(tolua_S, "ForEachEntityInBox",           ForEachInBox<cWorld, cEntity, &cWorld::ForEachEntityInBox>);
			tolua_function(tolua_S, "ForEachEntityInChunk",         tolua_cWorld_ForEachEntityInChunk);
			tolua_function(tolua_S, "ForEachFurnaceInChunk",        ForEachBlockEntityInChunk<cFurnaceEntity, E_BLOCK_FURNACE, E_BLOCK_LIT_FURNACE>);
			tolua_function(tolua_S, "ForEachLoadedChunk",           tolua_cWorld_ForEachLoadedChunk);
			tolua_function(tolua_S, "ForEachPlayer",                ForEach<cWorld, cPlayer, &cWorld::ForEachPlayer>);
			tolua_function(tolua_S, "GetBlock",                     tolua_cWorld_GetBlock);
			tolua_function(tolua_S, "GetBlockBlockLight",           tolua_cWorld_GetBlockBlockLight);
			tolua_function(tolua_S, "GetBlockInfo",                 tolua_cWorld_GetBlockInfo);
			tolua_function(tolua_S, "GetBlockMeta",                 tolua_cWorld_GetBlockMeta);
			tolua_function(tolua_S, "GetBlockSkyLight",             tolua_cWorld_GetBlockSkyLight);
			tolua_function(tolua_S, "GetBlockTypeMeta",             tolua_cWorld_GetBlockTypeMeta);
			tolua_function(tolua_S, "GetSignLines",                 tolua_cWorld_GetSignLines);
			tolua_function(tolua_S, "GetTimeOfDay",                 tolua_cWorld_GetTimeOfDay);
			tolua_function(tolua_S, "GetWorldAge",                  tolua_cWorld_GetWorldAge);
			tolua_function(tolua_S, "PrepareChunk",                 tolua_cWorld_PrepareChunk);
			tolua_function(tolua_S, "QueueTask",                    tolua_cWorld_QueueTask);
			tolua_function(tolua_S, "ScheduleTask",                 tolua_cWorld_ScheduleTask);
			tolua_function(tolua_S, "SetBlock",                     tolua_cWorld_SetBlock);
			tolua_function(tolua_S, "SetSignLines",                 tolua_cWorld_SetSignLines);
			tolua_function(tolua_S, "SetTimeOfDay",                 tolua_cWorld_SetTimeOfDay);
			tolua_function(tolua_S, "SpawnSplitExperienceOrbs",     tolua_cWorld_SpawnSplitExperienceOrbs);
			tolua_function(tolua_S, "TryGetHeight",                 tolua_cWorld_TryGetHeight);
		tolua_endmodule(tolua_S);
	tolua_endmodule(tolua_S);
}
