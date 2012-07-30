#ifndef FASTMAP_INLINE
#define FASTMAP_FUNC
#else
#define FASTMAP_FUNC static inline
#endif

/**
 * ARMul_LoadWordS
 *
 * Load Word, Sequential Cycle
 *
 * @param state
 * @param address
 * @returns
 */
FASTMAP_FUNC ARMword ARMul_LoadWordS(ARMul_State *state,ARMword address)
{
    FastMapEntry *entry;
    FastMapRes res;

	state->NumCycles++;
	address &= UINT32_C(0x3ffffff);

	entry = FastMap_GetEntryNoWrap(state,address);
	res = FastMap_DecodeRead(entry,state->FastMapMode);
	ARMul_CLEARABORT; /* More likely to clear the abort than not */
	if(FASTMAP_RESULT_DIRECT(res))
	{
		return *(FastMap_Log2Phy(entry,address&~UINT32_C(3)));
	}
	else if(FASTMAP_RESULT_FUNC(res))
	{
		return FastMap_LoadFunc(entry,state,address);
	}
	else
	{
		ARMul_DATAABORT(address);
		return 0;
	}
}

/**
 * ARMul_LoadByte
 *
 * Load Byte, (Non Sequential Cycle)
 *
 * @param state
 * @param address
 * @returns
 */
FASTMAP_FUNC ARMword ARMul_LoadByte(ARMul_State *state,ARMword address)
{
    FastMapEntry *entry;
    FastMapRes res;

	state->NumCycles++;
	address &= UINT32_C(0x3ffffff);

	entry = FastMap_GetEntryNoWrap(state,address);
	res = FastMap_DecodeRead(entry,state->FastMapMode);
	ARMul_CLEARABORT; /* More likely to clear the abort than not */
	if(FASTMAP_RESULT_DIRECT(res))
	{
#ifdef HOST_BIGENDIAN
		address ^= 3;
#endif
		return *((uint8_t*)FastMap_Log2Phy(entry,address));
	}
	else if(FASTMAP_RESULT_FUNC(res))
	{
		return (FastMap_LoadFunc(entry,state,address&~UINT32_C(3))>>((address&3)<<3)) & 0xff;
	}
	else
	{
		ARMul_DATAABORT(address);
		return 0;
	}
}

/**
 * ARMul_StoreWordS
 *
 * Store Word, Sequential Cycle
 *
 * @param state
 * @param address
 * @param data
 */
FASTMAP_FUNC void ARMul_StoreWordS(ARMul_State *state, ARMword address, ARMword data)
{
    FastMapEntry *entry;
    FastMapRes res;

	state->NumCycles++;
	address &= UINT32_C(0x3ffffff);

	entry = FastMap_GetEntryNoWrap(state,address);
	res = FastMap_DecodeWrite(entry,state->FastMapMode);
//  fprintf(stderr,"StoreWordS: %08x maps to entry %08x res %08x (mode %08x pc %08x)\n",address,entry,res,MEMC.FastMapMode,state->Reg[15]);
	ARMul_CLEARABORT;
	if(FASTMAP_RESULT_DIRECT(res))
	{
		ARMword *phy = FastMap_Log2Phy(entry,address&~UINT32_C(3));
		*phy = data;
		*(FastMap_Phy2Func(state,phy)) = FASTMAP_CLOBBEREDFUNC; 
	}
	else if(FASTMAP_RESULT_FUNC(res))
	{
		FastMap_StoreFunc(entry,state,address,data,FASTMAP_ACCESSFUNC_STATECHANGE);
	}
	else
	{
		ARMul_DATAABORT(address);
	}
}

/**
 * ARMul_StoreByte
 *
 * Store Byte, (Non Sequential Cycle)
 *
 * @param state
 * @param address
 * @param data
 */
FASTMAP_FUNC void ARMul_StoreByte(ARMul_State *state, ARMword address, ARMword data)
{
    FastMapEntry *entry;
    FastMapRes res;

	state->NumCycles++;
	address &= UINT32_C(0x3ffffff);

	entry = FastMap_GetEntryNoWrap(state,address);
	res = FastMap_DecodeWrite(entry,state->FastMapMode);
	ARMul_CLEARABORT;
	if(FASTMAP_RESULT_DIRECT(res))
	{
#ifdef HOST_BIGENDIAN
		address ^= 3;
#endif
		ARMword *phy = FastMap_Log2Phy(entry,address);
		*((uint8_t *)phy) = data;
		*(FastMap_Phy2Func(state,(ARMword*)(((FastMapUInt)phy)&~((FastMapUInt)3)))) = FASTMAP_CLOBBEREDFUNC;
	}
	else if(FASTMAP_RESULT_FUNC(res))
	{
		FastMap_StoreFunc(entry,state,address,data,FASTMAP_ACCESSFUNC_STATECHANGE | FASTMAP_ACCESSFUNC_BYTE);
	}
	else
	{
		ARMul_DATAABORT(address);
	}
}

/**
 * ARMul_SwapWord
 *
 * Swap Word, (Two Non Sequential Cycles)
 *
 * @param state
 * @param address
 * @param data
 * @returns
 */
FASTMAP_FUNC ARMword ARMul_SwapWord(ARMul_State *state, ARMword address, ARMword data)
{
    FastMapEntry *entry;
    FastMapRes res;
	ARMword temp;

	state->NumCycles+=2;
	address &= UINT32_C(0x3ffffff);

	entry = FastMap_GetEntryNoWrap(state,address);
	res = FastMap_DecodeWrite(entry,state->FastMapMode);
	ARMul_CLEARABORT;
	if(FASTMAP_RESULT_DIRECT(res))
	{
		ARMword *phy = FastMap_Log2Phy(entry,address&~UINT32_C(3));
		temp = *phy;
		*phy = data;
		*(FastMap_Phy2Func(state,phy)) = FASTMAP_CLOBBEREDFUNC;
		return temp;
	}
	else if(FASTMAP_RESULT_FUNC(res))
	{
		/* Assume we aren't trying to SWP ROM and use the read/write func regardless of whether we can perform a read via Log2Phy */
		temp = FastMap_LoadFunc(entry,state,address);
		FastMap_StoreFunc(entry,state,address,data,FASTMAP_ACCESSFUNC_STATECHANGE);
		return temp;
	}
	else
	{
		state->NumCycles--;
		ARMul_DATAABORT(address);
        return -1;
	}
}

/**
 * ARMul_SwapByte
 *
 * Swap Byte, (Two Non Sequential Cycles)
 *
 * @param state
 * @param address
 * @param data
 * @returns
 */
FASTMAP_FUNC ARMword ARMul_SwapByte(ARMul_State *state, ARMword address, ARMword data)
{
    FastMapEntry *entry;
    FastMapRes res;
	ARMword temp;

	state->NumCycles+=2;
	address &= UINT32_C(0x3ffffff);

	entry = FastMap_GetEntryNoWrap(state,address);
	res = FastMap_DecodeWrite(entry,state->FastMapMode);
	ARMul_CLEARABORT;
	if(FASTMAP_RESULT_DIRECT(res))
	{
#ifdef HOST_BIGENDIAN
		address ^= 3;
#endif
		ARMword *phy = FastMap_Log2Phy(entry,address);
		temp = *((uint8_t *)phy);
		*((uint8_t *)phy) = data;
		*(FastMap_Phy2Func(state,(ARMword*)(((FastMapUInt)phy)&~((FastMapUInt)3)))) = FASTMAP_CLOBBEREDFUNC;
		return temp;
	}
	else if(FASTMAP_RESULT_FUNC(res))
	{
		/* Assume we aren't trying to SWP ROM and use the read/write func regardless of whether we can perform a read via Log2Phy */
		temp = (FastMap_LoadFunc(entry,state,address)>>((address&3)<<3)) & 0xff;
		FastMap_StoreFunc(entry,state,address,data,FASTMAP_ACCESSFUNC_STATECHANGE | FASTMAP_ACCESSFUNC_BYTE);
		return temp;
	}
	else
	{
		state->NumCycles--;
		ARMul_DATAABORT(address);
        return -1;
	}
}
