#include <cstdio>
#include <cstdarg>
#include <igameevents.h>
#include <iserver.h>
#include <IPlayerHelpers.h>
#include "extension.h"

#define ARMS_ONLY_HANDS "models/weapons/v_models/arms/bare/v_bare_hands.mdl"
#define IGNORE_ANARCHIST "tm_anarchist"
#define ARMS_SZ_LEN 192
#define ARMS_FORCEUPDATE_TIMERDURATION 0.02

#define MaxClients gpGlobals->maxClients
#define CALL_FWD(A,B)	m_pOnArmsUpdated->PushCell(A); \
			m_pOnArmsUpdated->PushCell(B); \
			m_pOnArmsUpdated->Execute(NULL)

extern "C" void Warning(const char *pMsg, ...)
{
    va_list args;
    va_start(args, pMsg);
    std::vfprintf(stderr, pMsg, args);
    va_end(args);
}

extern "C++" void ConMsg(const char *pMsg, ...)
{
    va_list args;
    va_start(args, pMsg);
    std::vfprintf(stdout, pMsg, args);
    va_end(args);
}

extern "C" void Msg(const char *pMsg, ...)
{
    va_list args;
    va_start(args, pMsg);
    std::vfprintf(stdout, pMsg, args);
    va_end(args);
}

extern "C++" void DevMsg(const char *pMsg, ...)
{
    va_list args;
    va_start(args, pMsg);
    std::vfprintf(stdout, pMsg, args);
    va_end(args);
}

extern "C++" void DevWarning(const char *pMsg, ...)
{
    va_list args;
    va_start(args, pMsg);
    std::vfprintf(stderr, pMsg, args);
    va_end(args);
}

extern "C++" void ConColorMsg(const Color &clr, const char *pMsg, ...)
{
    (void)clr;
    va_list args;
    va_start(args, pMsg);
    std::vfprintf(stdout, pMsg, args);
    va_end(args);
}

// Variables
IGameEventManager2 *gameevents = NULL;
IForward *m_pOnArmsUpdated;

int iSavedActiveWeapon[64+1];
char szPlayerArmsModels_Default[ARMS_SZ_LEN] = ARMS_ONLY_HANDS;
bool bPlayerDisableArmsUpdate[64+1];
char szPlayerArmsModels[64+1][ARMS_SZ_LEN];
int iArmsModelOffset = -1, iActiveWeaponOffset = -1;

// SourceMod related
ArmsFix g_ArmsFix;
SMEXT_LINK(&g_ArmsFix);
SH_DECL_HOOK2(IVEngineServer, PrecacheModel, SH_NOATTRIB, 0, int, const char *, bool);
SH_DECL_HOOK3_void(IServerGameDLL, ServerActivate, SH_NOATTRIB, 0, edict_t *, int, int);

// Functions
void PrecacheDefaultArms(bool preload = true)
{
    engine->PrecacheModel(szPlayerArmsModels_Default, preload);
}

bool ArmsFix::SDK_OnMetamodLoad(ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
    GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer, INTERFACEVERSION_VENGINESERVER);
    GET_V_IFACE_CURRENT(GetEngineFactory, gameevents, IGameEventManager2, INTERFACEVERSION_GAMEEVENTSMANAGER2);

    SH_ADD_HOOK(IVEngineServer, PrecacheModel, engine, SH_MEMBER(this, &ArmsFix::PrecacheModel), false);
    SH_ADD_HOOK(IServerGameDLL, ServerActivate, gamedll, SH_MEMBER(this, &ArmsFix::OnServerActivate), true);

    gameevents->AddListener(this, "player_spawn", true);
    gameevents->AddListener(this, "player_disconnect", true);
    gameevents->AddListener(this, "player_activate", true);
    return true;
}

void ArmsFix::FireGameEvent(IGameEvent *pEvent)
{
    if (!pEvent) // Who knows what's gonna happen?
        return;

    const char *name = pEvent->GetName();
    int iClient;

    if (name[7] == 's') // player_spawn
    {
        iClient = playerhelpers->GetClientOfUserId(pEvent->GetInt("userid"));
        if(!iClient || bPlayerDisableArmsUpdate[iClient])
            return;
        CBaseEntity *pPlayer = gamehelpers->ReferenceToEntity(iClient);
	if (!pPlayer)
            return;
	char *dest = (char*)((uint8_t*) pPlayer + iArmsModelOffset);
        CALL_FWD(iClient, 0);
        ke::SafeStrcpy(dest, ARMS_SZ_LEN, szPlayerArmsModels[iClient][0]?szPlayerArmsModels[iClient]:szPlayerArmsModels_Default);
    	return;
    }
    // We dont need a check. Since there's only Spawn AND (Disconnect OR Activate)
    // Deleting saved things for clients
    iClient = playerhelpers->GetClientOfUserId(pEvent->GetInt("userid"));
    if(!iClient)
        return;
    szPlayerArmsModels[iClient][0] = '\0';
    bPlayerDisableArmsUpdate[iClient] = false;
    iSavedActiveWeapon[iClient] = -1;
}

bool ArmsFix::SDK_OnLoad(char *error, size_t maxlength, bool late)
{
    sm_sendprop_info_t info;
    if (!gamehelpers->FindSendPropInfo("CCSPlayer", "m_szArmsModel", &info))
    {
        Q_snprintf(error, maxlength, "Couldn't find CCSPlayer::m_szArmsModel offset!");
        return false;
    }
    iArmsModelOffset = info.actual_offset;
    if (!gamehelpers->FindSendPropInfo("CCSPlayer", "m_hActiveWeapon", &info))
    {
        Q_snprintf(error, maxlength, "Couldn't find CCSPlayer::m_hActiveWeapon offset!");
        return false;
    }
    iActiveWeaponOffset = info.actual_offset;

    m_pOnArmsUpdated = forwards->CreateForward("AF_OnArmsUpdate", ET_Ignore, 2, NULL, Param_Cell, Param_Cell);
    PrecacheDefaultArms(true);
    return true;
}

int ArmsFix::PrecacheModel(const char *model, bool precache)
{
    if (V_strncmp(model, "models/weapons/v_models/arms/glove_har", 38) == 0 || /* CT Arms */
        V_strncmp(model, "models/weapons/v_models/arms/glove_f", 36) == 0 || /* T Arms */
        V_strncmp(model, "models/weapons/v_models/arms/ph", 31) == 0 /* Very thicc heavy phoenix sleeves */
    )
    {
        RETURN_META_VALUE(MRES_SUPERCEDE, 0);
    }
    RETURN_META_VALUE(MRES_IGNORED, 0);
}

void ArmsFix::OnServerActivate(edict_t *pEdictList, int edictCount, int clientMax)
{
    PrecacheDefaultArms(true);
}

void ArmsFix::SDK_OnUnload()
{
    SH_REMOVE_HOOK(IVEngineServer, PrecacheModel, engine, SH_MEMBER(this, &ArmsFix::PrecacheModel), false);
    SH_REMOVE_HOOK(IServerGameDLL, ServerActivate, gamedll, SH_MEMBER(this, &ArmsFix::OnServerActivate), true);

    forwards->ReleaseForward(m_pOnArmsUpdated);
    gameevents->RemoveListener( this );
}

/* ============================================== Natives ============================================== */
// native int AF_Version();
cell_t sm_AF_Version(IPluginContext *pContext, const cell_t *params)
{
    return SMEXT_CONF_CUSTOM_VERCODE;
}

// native void AF_SetDefaultArmsModel(char[] mdl_path);
cell_t sm_AF_SetDefaultArmsModel(IPluginContext *pContext, const cell_t *params)
{
    static char *szMdlPath;
    pContext->LocalToString(params[1], &szMdlPath);
    ke::SafeStrcpy(szPlayerArmsModels_Default, ARMS_SZ_LEN, szMdlPath);
    engine->PrecacheModel(szPlayerArmsModels_Default, true);
    return 1;
}

// native void AF_ResetDefaultArmsModel();
cell_t sm_AF_ResetDefaultArmsModel(IPluginContext *pContext, const cell_t *params)
{
    ke::SafeStrcpy(szPlayerArmsModels_Default, ARMS_SZ_LEN, ARMS_ONLY_HANDS);
    return 1;
}

// native bool AF_HasClientCustomArms(int client);
cell_t sm_AF_HasClientCustomArms(IPluginContext *pContext, const cell_t *params)
{
    int iClient = params[1];
    if(iClient < 1 || iClient > 64)
        return pContext->ThrowNativeError("Wrong client index (%d)", iClient);
    return szPlayerArmsModels[iClient][0]?true:false;
}

// native void AF_SetClientArmsModel(int client, char[] mdl_path);
cell_t sm_AF_SetClientArmsModel(IPluginContext *pContext, const cell_t *params)
{
    int iClient = params[1];
    if(iClient < 1 || iClient > 64)
        return pContext->ThrowNativeError("Wrong client index (%d)", iClient);
    char *szMdlPath;
    pContext->LocalToString(params[2], &szMdlPath);
    engine->PrecacheModel(szMdlPath, true);
    CALL_FWD(iClient, 4);
    ke::SafeStrcpy(szPlayerArmsModels[iClient], ARMS_SZ_LEN, szMdlPath);
    return 1;
}

// native void AF_RemoveClientArmsModel(int client);
cell_t sm_AF_RemoveClientArmsModel(IPluginContext *pContext, const cell_t *params)
{
    int iClient = params[1];
    if(iClient < 1 || iClient > 64)
        return pContext->ThrowNativeError("Wrong client index (%d)", iClient);
    szPlayerArmsModels[iClient][0] = '\0';
    return 1;
}

// native int AF_GetClientArmsModel(int client, char[] dest, int maxlen);
cell_t sm_AF_GetClientArmsModel(IPluginContext *pContext, const cell_t *params)
{
    int iClient = params[1];
    if(iClient < 1 || iClient > 64)
        return pContext->ThrowNativeError("Wrong client index (%d)", iClient);
    CBaseEntity *pPlayer = gamehelpers->ReferenceToEntity(iClient);
    if (!pPlayer)
        return pContext->ThrowNativeError("Cannot allocate CBaseEntity pointer of a player (%d)", iClient);
    size_t len;
    pContext->StringToLocalUTF8(params[2], params[3], szPlayerArmsModels[iClient][0]?szPlayerArmsModels[iClient]:szPlayerArmsModels_Default, &len);
    return len;
}

// native int AF_GetDefaultArmsModel(char[] dest, int maxlen);
cell_t sm_AF_GetDefaultArmsModel(IPluginContext *pContext, const cell_t *params)
{
    size_t len;
    pContext->StringToLocalUTF8(params[1], params[2], szPlayerArmsModels_Default, &len);
    return len;
}

static void FrameAction_SetClientActiveWeapon_End(void* pData)
{
    int iClient = (uintptr_t) pData;
    if(iSavedActiveWeapon[iClient] < 64)
        return;
    CBaseEntity *pPlayer = gamehelpers->ReferenceToEntity(iClient);
    if (!pPlayer)
        return;
    CBaseHandle &hndl = *(CBaseHandle*)((uint8_t *)pPlayer + iActiveWeaponOffset);
    CBaseEntity *pOther = gamehelpers->ReferenceToEntity(iSavedActiveWeapon[iClient]);
    if(pOther)
        hndl.Set((IHandleEntity*) pOther);
}

static void FrameAction_SetClientActiveWeapon_Middle(void* pData)
{
    smutils->AddFrameAction(FrameAction_SetClientActiveWeapon_End, pData);
}

static void FrameAction_SetClientActiveWeapon(void* pData)
{
    smutils->AddFrameAction(FrameAction_SetClientActiveWeapon_Middle, pData);
}

// native bool AF_RequestArmsUpdate(int client, bool force = false);
cell_t sm_AF_RequestArmsUpdate(IPluginContext *pContext, const cell_t *params)
{
    int iClient = params[1];
    if(iClient < 1 || iClient > 64)
        return pContext->ThrowNativeError("Wrong client index (%d)", iClient);
    CBaseEntity *pPlayer = gamehelpers->ReferenceToEntity(iClient);
    if (!pPlayer)
        return pContext->ThrowNativeError("Client (%d) is not connected or missing", iClient);
    if(bPlayerDisableArmsUpdate[iClient])
    {
        CALL_FWD(iClient, 2);
        ke::SafeStrcpy((char *) ((uint8_t *) pPlayer + iArmsModelOffset), ARMS_SZ_LEN, "");
        return false;
    }
    if(params[2])
    {
        CBaseHandle &hndl = *(CBaseHandle*)((uint8_t *)pPlayer + iActiveWeaponOffset);
        CBaseEntity *pHandleEntity = gamehelpers->ReferenceToEntity(hndl.GetEntryIndex());
        if (pHandleEntity && hndl == reinterpret_cast<IHandleEntity*>(pHandleEntity)->GetRefEHandle())
        {
            iSavedActiveWeapon[iClient] = gamehelpers->EntityToBCompatRef(pHandleEntity);
            hndl.Set(NULL);
            smutils->AddFrameAction(FrameAction_SetClientActiveWeapon, (uintptr_t*)iClient);
        }
    }
    CALL_FWD(iClient, 1);
    ke::SafeStrcpy((char *) ((uint8_t *) pPlayer + iArmsModelOffset), ARMS_SZ_LEN, szPlayerArmsModels[iClient][0]?szPlayerArmsModels[iClient]:szPlayerArmsModels_Default);

    return true;
}

// native void AF_DisableClientArmsUpdate(int client, bool disable = true, bool remove_arms = true);
cell_t sm_AF_DisableClientArmsUpdate(IPluginContext *pContext, const cell_t *params)
{
    int iClient = params[1];
    if(iClient < 1 || iClient > 64)
        return pContext->ThrowNativeError("Wrong client index (%d)", iClient);
    CBaseEntity *pPlayer = gamehelpers->ReferenceToEntity(iClient);
    if (!pPlayer)
        return pContext->ThrowNativeError("Client (%d) is not connected or missing", iClient);

    bPlayerDisableArmsUpdate[iClient] = params[2];
    if(params[2] && params[3])
    {
        CALL_FWD(iClient, 3);
        ke::SafeStrcpy((char *) ((uint8_t *) pPlayer + iArmsModelOffset), ARMS_SZ_LEN, "");
    }
    return 1;
}

// native bool AF_IsClientArmsNotUpdating(int client);
cell_t sm_AF_IsClientArmsNotUpdating(IPluginContext *pContext, const cell_t *params)
{
    int iClient = params[1];
    if(iClient < 1 || iClient > 64)
        return pContext->ThrowNativeError("Wrong client index (%d)", iClient);
    return bPlayerDisableArmsUpdate[iClient];
}

// native bool AF_ForceArmsUpdate(int client, bool ignore_blocked = false);
cell_t sm_AF_ForceArmsUpdate(IPluginContext *pContext, const cell_t *params)
{
    int iClient = params[1];
    if(iClient < 1 || iClient > 64)
        return pContext->ThrowNativeError("Wrong client index (%d)", iClient);
    if(bPlayerDisableArmsUpdate[iClient] && !params[2])
        return false;
    CBaseEntity *pPlayer = gamehelpers->ReferenceToEntity(iClient);
    if (!pPlayer)
        return pContext->ThrowNativeError("Client (%d) is not connected or missing", iClient);
    CBaseHandle &hndl = *(CBaseHandle*)((uint8_t *)pPlayer + iActiveWeaponOffset);
    CBaseEntity *pHandleEntity = gamehelpers->ReferenceToEntity(hndl.GetEntryIndex());
    if (pHandleEntity && hndl == reinterpret_cast<IHandleEntity*>(pHandleEntity)->GetRefEHandle())
    {
        iSavedActiveWeapon[iClient] = gamehelpers->EntityToBCompatRef(pHandleEntity);
        hndl.Set(NULL);
        smutils->AddFrameAction(FrameAction_SetClientActiveWeapon, (uintptr_t*)iClient);
        return true;
    }
    return false;
}

const sp_nativeinfo_t NativesList[] = 
{
	{"AF_Version", sm_AF_Version},
	{"AF_SetDefaultArmsModel", sm_AF_SetDefaultArmsModel},
	{"AF_ResetDefaultArmsModel", sm_AF_ResetDefaultArmsModel},
	{"AF_SetClientArmsModel", sm_AF_SetClientArmsModel},
	{"AF_HasClientCustomArms", sm_AF_HasClientCustomArms},
	{"AF_RemoveClientArmsModel", sm_AF_RemoveClientArmsModel},
	{"AF_GetClientArmsModel", sm_AF_GetClientArmsModel},
	{"AF_GetDefaultArmsModel", sm_AF_GetDefaultArmsModel},
	{"AF_RequestArmsUpdate", sm_AF_RequestArmsUpdate},
	{"AF_DisableClientArmsUpdate", sm_AF_DisableClientArmsUpdate},
	{"AF_IsClientArmsNotUpdating", sm_AF_IsClientArmsNotUpdating},
	{"AF_ForceArmsUpdate", sm_AF_ForceArmsUpdate},
	{NULL, NULL},
};

void ArmsFix::SDK_OnAllLoaded()
{
    sharesys->AddNatives(myself, NativesList);
}
