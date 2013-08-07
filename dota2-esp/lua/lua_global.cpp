#include "precompiled_headers.h"

#include "lua_global.hpp"

#include <functional>
#include <algorithm>

#include "..\source-sdk\SDK.h"
#include "..\source-sdk\global_instance_manager.h"

#include "..\dota\DotaGlobal.h"

#include "..\dota\DotaGlobal.h"
#include "..\utils\utils.h"
#include "..\utils\vmthooks.h"
#include "..\source-sdk\draw_utils.h"

#include "..\..\LuaBridge\RefCountedPtr.h"

#undef CreateEvent

namespace lua {

  class LuaGameEventHelper {
   public:
     LuaGameEventHelper(const char* name, luabridge::LuaRef callback) : listener_(callback) {
       listener_.ListenForGameEvent(name);
     }
  private:
    CLuaGameEventListener listener_;
  };

  class HookListener {
   public:
    virtual void NewEvent() = 0;
  };
  class HookManager {
   public:
    void AddListener(HookListener* listener) {
      callbacks_.push_back(listener);
    }

    void RemoveListener(HookListener* listener) {
      std::list<HookListener*>::iterator it = std::find(callbacks_.begin(), callbacks_.end(), listener);
      if (it != callbacks_.end()) {
        callbacks_.erase(it);
      }
    }

    void CallListener() {
      for( auto listener : callbacks_) {
        listener->NewEvent();
      }
    }

    virtual ~HookManager() {
      if (hook_) {
        hook_->Unhook();
        delete hook_;
      }
    }
    protected:
     utils::VtableHook* hook_;
     std::list<HookListener*> callbacks_;
  };
  class PaintHookManager : public HookManager {
   public:
    static PaintHookManager& GetInstance() {
       static PaintHookManager instance;
       return instance;
    }
   private:
     PaintHookManager() {
       hook_ = new utils::VtableHook(CHud::GetInstance()->FindElement("CHudHealthBars"), 0x34);
       hook_->HookMethod(CHudHealthBars_Paint, 107);
     }
     static void __fastcall CHudHealthBars_Paint(void* thisptr, int edx, void* guipaintsurface) {
       PaintHookManager& hook_manager = PaintHookManager::GetInstance();
       typedef void ( __thiscall* OriginalFunction )(PVOID, PVOID);
       hook_manager.hook_->GetMethod<OriginalFunction>(107)(thisptr, guipaintsurface);
       
       hook_manager.CallListener();
     }
  };
  class PaintHookListener : public HookListener {
  public:
    PaintHookListener(luabridge::LuaRef callback) : callback_(callback) {
      PaintHookManager::GetInstance().AddListener(this);
    }
    ~PaintHookListener() {
      PaintHookManager::GetInstance().RemoveListener(this);
    }
    void NewEvent() {
      try {
        callback_();
      }
      catch (luabridge::LuaException const& e) {
        Warning("booster-lua: %s \n", e.what());
      }
    }
  private:
    luabridge::LuaRef callback_;
  };

  class LuaParticleEffect {
   public:
    LuaParticleEffect(CNewParticleEffect* particle_effect, CParticleProperty* prop) : effect_(particle_effect), prop_(prop) {
      Warning("create particle \n");
    }
    void SetControlPoint(int point, Vector vec) {
      effect_->SetControlPoint(point, vec);
    }
    ~LuaParticleEffect() {
      Warning("destroy particle \n");
      if (effect_ != nullptr && prop_ != nullptr) prop_->StopEmissionAndDestroyImmediately(effect_);
    }
   private:
     CNewParticleEffect* effect_;
     CParticleProperty* prop_;
  };

  class LuaParticleProp {
   public:
    LuaParticleProp(CParticleProperty* particle_property) : prop_(particle_property) {

    }

    RefCountedPtr<LuaParticleEffect> CreateEffect(const char* name) {
      return new LuaParticleEffect(prop_->Create(name, 1, -1), prop_);
    }
   private:
     CParticleProperty* prop_;
  };

  class LuaBaseNpc : public CBaseNpc {
  public:
    LuaParticleProp GetLuaParticleProp() {
      return LuaParticleProp(GetParticleProp());
    }
  };

  class EntityListHelper {
  public:
    static luabridge::LuaRef FindEntities(int type,  lua_State* L) {
      luabridge::LuaRef table = luabridge::newTable(L);
      int count = 1;
      for (int i = 1; i < GlobalInstanceManager::GetClientEntityList()->GetHighestEntityIndex(); i++ ) {
        CBaseEntity *base_entity = GlobalInstanceManager::GetClientEntityList()->GetClientEntity(i);
        if (base_entity == nullptr) continue;

        const char* class_name = base_entity->GetClientClass()->GetName();
        if (class_name == nullptr) continue;

        if (StringHasPrefix(class_name, "CDOTA_BaseNPC_Tower")) {
          LuaBaseNpc* tower = (LuaBaseNpc*)base_entity;
          table[count] = tower;
          count++;
        }
    }
      return table;
    }
  };

  typedef void(*MsgSignature)(const tchar* msg, ...); 
  template<MsgSignature T>
  int  LogWrapper(lua_State *L) {
    int argc = lua_gettop(L);
    T(lua_tostring(L, 1));
    return 0;
  }
  void RegisterGlobalFunctions(LuaState& state) {
    luabridge::getGlobalNamespace (state)
      .beginNamespace("log")
        .addCFunction("Msg", LogWrapper<Msg>)
        .addCFunction("Warning", LogWrapper<Warning>)
        .addCFunction("Error", LogWrapper<Error>)
      .endNamespace();

    luabridge::getGlobalNamespace(state)
      .beginNamespace("dota")
        .beginClass<LuaGameEventHelper>("AddEventListener")
         .addConstructor<void (*) (const char*, luabridge::LuaRef)> ()
        .endClass()
      .endNamespace();

    luabridge::getGlobalNamespace(state)
      .beginNamespace("dota")
        .beginClass<PaintHookListener>("AddPaintHook")
         .addConstructor<void (*) (luabridge::LuaRef)> ()
        .endClass()
      .endNamespace();

    luabridge::getGlobalNamespace(state)
      .beginNamespace("dota")
        .beginClass<EntityListHelper>("EntityListHelper")
          .addStaticFunction("FindEntities", &EntityListHelper::FindEntities)
        .endClass()
      .endNamespace();

    luabridge::getGlobalNamespace(state)
      .beginNamespace("dota")
        .beginClass<sourcesdk::DrawUtils>("DrawUtils")
          .addStaticFunction("GetInstance", &sourcesdk::DrawUtils::GetInstance)
          .addFunction("DrawRect", &sourcesdk::DrawUtils::DrawRect)
        .endClass()
      .endNamespace();

    luabridge::getGlobalNamespace(state)
      .beginNamespace("dota")
        .beginClass<GlobalInstanceManager>("GlobalInstanceManager")
         .addStaticFunction("GetClient", &GlobalInstanceManager::GetClient)
         .addStaticFunction("GetClientEntityList", &GlobalInstanceManager::GetClientEntityList)
         .addStaticFunction("GetEngineClient", &GlobalInstanceManager::GetEngineClient)
         .addStaticFunction("GetSurface", &GlobalInstanceManager::GetSurface)
         .addStaticFunction("GetSurfaceNew", &GlobalInstanceManager::GetSurfaceNew)
         .addStaticFunction("GetPanel", &GlobalInstanceManager::GetPanel)
         .addStaticFunction("GetClientTools", &GlobalInstanceManager::GetClientTools)
         .addStaticFunction("GetCVar", &GlobalInstanceManager::GetCVar)
         .addStaticFunction("GetVguiInput", &GlobalInstanceManager::GetVguiInput)
         .addStaticFunction("GetLocalize", &GlobalInstanceManager::GetLocalize)
         .addStaticFunction("GetGameEventManager", &GlobalInstanceManager::GetGameEventManager)
        .endClass()
      .endNamespace();

    luabridge::getGlobalNamespace(state)
      .beginNamespace("dota")
        .beginClass<EngineClient>("EngineClient")
         .addFunction("Con_IsVisible", &EngineClient::Con_IsVisible)
         .addFunction("GetLocalPlayerIndex", &EngineClient::GetLocalPlayerIndex)
         .addFunction("Time", &EngineClient::Time)
         .addFunction("GetMaxClients", &EngineClient::GetMaxClients)
         .addFunction("IsInGame", &EngineClient::IsInGame)
         .addFunction("IsConnected", &EngineClient::IsConnected)
         .addFunction("IsDrawingLoadingImage", &EngineClient::IsDrawingLoadingImage)
         .addFunction("WorldToScreenMatrix", &EngineClient::WorldToScreenMatrix)
         .addFunction("IsTakingScreenshot", &EngineClient::IsTakingScreenshot)
         .addFunction("ClientCmd_Unrestricted", &EngineClient::ClientCmd_Unrestricted)
        .endClass()
      .endNamespace();

    luabridge::getGlobalNamespace(state)
      .beginNamespace("dota")
        .beginClass<ClientTools>("ClientTools")
         .addFunction("GetLocalPlayer", &ClientTools::GetLocalPlayer)
        .endClass()
      .endNamespace();

    luabridge::getGlobalNamespace(state)
      .beginNamespace("dota")
        .beginClass<Vgui_IInput>("Vgui_IInput")
         .addFunction("IsKeyDown", &Vgui_IInput::IsKeyDown)
        .endClass()
      .endNamespace();

    luabridge::getGlobalNamespace(state)
      .beginNamespace("dota")
        .beginClass<CGameEventListener>("CGameEventListener")
         .addFunction("ListenForGameEvent", &CGameEventListener::ListenForGameEvent)
         .addFunction("StopListeningForAllEvents", &CGameEventListener::StopListeningForAllEvents)
        .endClass()
      .endNamespace();

    luabridge::getGlobalNamespace(state)
      .beginNamespace("dota")
        .beginClass<IGameEvent>("IGameEvent")
         .addFunction("GetName", &IGameEvent::GetName)
         .addFunction("IsReliable", &IGameEvent::IsReliable)
         .addFunction("IsLocal", &IGameEvent::IsLocal)
         .addFunction("IsEmpty", &IGameEvent::IsEmpty)
         .addFunction("GetBool", &IGameEvent::GetBool)
         .addFunction("GetInt", &IGameEvent::GetInt)
         .addFunction("GetFloat", &IGameEvent::GetFloat)
         .addFunction("GetString", &IGameEvent::GetString)
         .addFunction("SetBool", &IGameEvent::SetBool)
         .addFunction("SetInt", &IGameEvent::SetInt)
         .addFunction("SetFloat", &IGameEvent::SetFloat)
         .addFunction("SetString", &IGameEvent::SetString)
        .endClass()
      .endNamespace();

    luabridge::getGlobalNamespace(state)
      .beginNamespace("dota")
        .beginClass<ILocalize>("ILocalize")
         .addFunction("Find", &ILocalize::Find)
        .endClass()
      .endNamespace();

    luabridge::getGlobalNamespace(state)
      .beginNamespace("dota")
        .beginClass<EntityList>("EntityList")
         .addFunction("GetClientEntity", &EntityList::GetClientEntity)
         .addFunction("GetClientEntityFromHandle", &EntityList::GetClientEntityFromHandle)
         .addFunction("GetHighestEntityIndex", &EntityList::GetHighestEntityIndex)
        .endClass()
      .endNamespace();

    luabridge::getGlobalNamespace(state)
      .beginNamespace("dota")
        .beginClass<IGameEventManager2>("IGameEventManager2")
         .addFunction("LoadEventsFromFile", &IGameEventManager2::LoadEventsFromFile)
         .addFunction("Reset", &IGameEventManager2::Reset)
         .addFunction("AddListener", &IGameEventManager2::AddListener)
         .addFunction("FindListener", &IGameEventManager2::FindListener)
         .addFunction("CreateEvent", &IGameEventManager2::CreateEvent)
         .addFunction("FireEvent", &IGameEventManager2::FireEvent)
         .addFunction("FireEventClientSide", &IGameEventManager2::FireEventClientSide)
         .addFunction("DuplicateEvent", &IGameEventManager2::DuplicateEvent)
         .addFunction("FreeEvent", &IGameEventManager2::FreeEvent)
         .addFunction("SerializeEvent", &IGameEventManager2::SerializeEvent)
         .addFunction("UnserializeEvent", &IGameEventManager2::UnserializeEvent)
        .endClass()
      .endNamespace();

    luabridge::getGlobalNamespace(state)
      .beginNamespace("dota")
        .beginClass<CUnitInventory>("CUnitInventory")
         .addFunction("GetItemInSlot", &CUnitInventory::GetItemInSlot)
        .endClass()
     .endNamespace();

    luabridge::getGlobalNamespace(state)
      .beginNamespace("dota")
        .beginClass<Vector>("Vector")
         .addConstructor<void (*) (float, float, float)>()
         .addData("x", &Vector::x)
         .addData("y", &Vector::x)
         .addData("z", &Vector::x)
         .addFunction("IsValid", &Vector::IsValid)
         .addFunction("Invalidate", &Vector::Invalidate)
         .addFunction("Zero", &Vector::Zero)
         .addFunction("Length", &Vector::Length)
         .addFunction("DistTo", &Vector::DistTo)
        .endClass()
     .endNamespace();

    luabridge::getGlobalNamespace(state)
      .beginNamespace("dota")
        .beginClass<CNewParticleEffect>("CNewParticleEffect")
         .addFunction("SetControlPoint", &CNewParticleEffect::SetControlPoint)
        .endClass()
     .endNamespace();
    luabridge::getGlobalNamespace(state)
      .beginNamespace("dota")
        .beginClass<LuaParticleEffect>("LuaParticleEffect")
         .addFunction("SetControlPoint", &LuaParticleEffect::SetControlPoint)
        .endClass()
     .endNamespace();

    luabridge::getGlobalNamespace(state)
      .beginNamespace("dota")
        .beginClass<LuaParticleProp>("LuaParticleProp")
         .addFunction("Create", &LuaParticleProp::CreateEffect)
        .endClass()
     .endNamespace();

    luabridge::getGlobalNamespace(state)
      .beginNamespace("dota")
        .beginClass<CBaseEntity>("CBaseEntity")
         .addFunction("GetRefEHandle", &CBaseEntity::GetRefEHandle)
         .addFunction("GetClientClass", &CBaseEntity::GetClientClass)
         .addFunction("ComputeTranslucencyType", &CBaseEntity::ComputeTranslucencyType)
         .addFunction("IsAlive", &CBaseEntity::IsAlive)
         .addFunction("GetTeamIndex", &CBaseEntity::GetTeamIndex)
         .addFunction("GetHealth", &CBaseEntity::GetHealth)
         .addFunction("IsPlayer", &CBaseEntity::IsPlayer)
         .addFunction("GetAbsOrigin", &CBaseEntity::GetAbsOrigin)
         .addFunction("GetAbsAngles", &CBaseEntity::GetAbsAngles)
         .addFunction("GetIndex", &CBaseEntity::GetIndex)
        .endClass()
        .deriveClass <LuaBaseNpc, CBaseEntity> ("LuaBaseNpc")
          .addFunction ("GetParticleProp", &LuaBaseNpc::GetLuaParticleProp)
          .addFunction ("GetEffectiveInvisibilityLevel", &LuaBaseNpc::GetEffectiveInvisibilityLevel)
          .addFunction ("IsVisibleByEnemyTeam", &LuaBaseNpc::IsVisibleByEnemyTeam)
          .addFunction ("GetHealth", &LuaBaseNpc::GetHealth)
          .addFunction ("GetVisibilityLevel", &LuaBaseNpc::GetVisibilityLevel)
          .addFunction ("SetVisibilityLevel", &LuaBaseNpc::SetVisibilityLevel)
          .addFunction ("GetMana", &LuaBaseNpc::GetMana)
          .addFunction ("GetMaxMana", &LuaBaseNpc::GetMaxMana)
          .addFunction ("GetInventory", &LuaBaseNpc::GetInventory)
          .addFunction ("GetAbilityByIndex", &LuaBaseNpc::GetAbilityByIndex)
          .addFunction ("GetModifierManager", &LuaBaseNpc::GetModifierManager)
          .addFunction ("GetPhysicalArmor", &LuaBaseNpc::GetPhysicalArmor)
          .addFunction ("IsEntityLastHittable", &LuaBaseNpc::IsEntityLastHittable)
          .addFunction ("GetPhysicalArmorReduction", &LuaBaseNpc::GetPhysicalArmorReduction)
          .addFunction ("GetDamageMin", &LuaBaseNpc::GetDamageMin)
          .addFunction ("GetDamageMax", &LuaBaseNpc::GetDamageMax)
          .addFunction ("GetBonusDamage", &LuaBaseNpc::GetBonusDamage)
          .addFunction ("GetAttackRange", &LuaBaseNpc::GetAttackRange)
          .addFunction ("IsEntityInRange", &LuaBaseNpc::IsEntityInRange)
        .endClass ()
        .deriveClass <CBaseNpcHero, LuaBaseNpc> ("CBaseNpcHero")
          .addFunction ("IsIllusion", &CBaseNpcHero::IsIllusion)
        .endClass ()
        .deriveClass <CDotaItem, CBaseEntity> ("CDotaItem")
          .addFunction ("GetName", &CDotaItem::GetName)
          .addFunction ("GetItemId", &CDotaItem::GetItemId)
          .addFunction ("GetRequiresCharges", &CDotaItem::GetRequiresCharges)
          .addFunction ("GetCurrentCharges", &CDotaItem::GetCurrentCharges)
          .addFunction ("GetInitialCharges", &CDotaItem::GetInitialCharges)
          .addFunction ("GetPurchaser", &CDotaItem::GetPurchaser)
          .addFunction ("GetPurchaseTime", &CDotaItem::GetPurchaseTime)
        .endClass ()
      .endNamespace();

    luabridge::getGlobalNamespace(state)
      .beginNamespace("dota")
        .beginClass<ClientClass>("ClientClass")
         .addFunction("GetName", &ClientClass::GetName)
        .endClass()
      .endNamespace();
  }
}