#pragma once

#include <fast_dynamic_cast/fast_dynamic_cast.hpp>

// demonized: smart_cast with specializations modern c++ way
// Fallback uses fast_dynamic_cast library

// Helper to detect if a type has a dedicated cast method
// Specializations will be provided for known dcast methods
template<typename _To, typename _From, typename = void>
struct has_dcast : std::false_type {};

#define DECLARE_SPECIALIZATION(TO, FROM, METHOD) \
template<> struct has_dcast<TO*, FROM*> : std::true_type { \
    static_assert(std::is_convertible_v<decltype(std::declval<FROM*>()->METHOD()), TO*>, \
        "Specialization Error: " #METHOD " does not return " #TO "*"); \
    static TO* cast(FROM* ptr) { return ptr ? ptr->METHOD() : nullptr; } \
};

#include "../include/xrRender/RenderVisual.h"
#include "../include/xrRender/Kinematics.h"
#include "../include/xrRender/KinematicsAnimated.h"
#include "../include/xrRender/ParticleCustom.h"

DECLARE_SPECIALIZATION(IKinematics, IRenderVisual, dcast_PKinematics)
DECLARE_SPECIALIZATION(IKinematicsAnimated, IRenderVisual, dcast_PKinematicsAnimated)
DECLARE_SPECIALIZATION(IKinematics, IKinematicsAnimated, dcast_PKinematics)
DECLARE_SPECIALIZATION(IKinematicsAnimated, IKinematics, dcast_PKinematicsAnimated)
DECLARE_SPECIALIZATION(IRenderVisual, IKinematics, dcast_RenderVisual)
DECLARE_SPECIALIZATION(IRenderVisual, IKinematicsAnimated, dcast_RenderVisual)
DECLARE_SPECIALIZATION(IParticleCustom, IRenderVisual, dcast_ParticleCustom)

// Project-specific specializations
#ifdef XRGAME_EXPORTS
#include "../xrGame/GameObject.h"
#include "../xrGame/InventoryOwner.h"
#include "../xrGame/inventory_item.h"
#include "../xrGame/Weapon.h"
#include "../xrGame/WeaponStatMgun.h"
#include "../xrGame/CustomRocket.h"
#include "xrServer_Objects_ALife.h"
#include "xrServer_Objects_ALife_Monsters.h"

class CObject;
class CGameObject;
class CEntity;
class CEntityAlive;
class CInventoryItem;
class CInventoryOwner;
class CHudItem;
class CFoodItem;
class CWeapon;
class CWeaponMagazined;
class CWeaponAmmo;
class CSE_ALifeTraderAbstract;
class CWeaponStatMgun;
class CHangingLamp;
class CCustomRocket;

// Original specializations
DECLARE_SPECIALIZATION(IRenderable, ISpatial, dcast_Renderable)
DECLARE_SPECIALIZATION(IRender_Light, ISpatial, dcast_Light)
DECLARE_SPECIALIZATION(CObject, ISpatial, dcast_CObject)
DECLARE_SPECIALIZATION(Feel::Sound, ISpatial, dcast_FeelSound)
DECLARE_SPECIALIZATION(CEntity, CGameObject, cast_entity)
DECLARE_SPECIALIZATION(CEntityAlive, CGameObject, cast_entity_alive)
DECLARE_SPECIALIZATION(CInventoryItem, CGameObject, cast_inventory_item)
DECLARE_SPECIALIZATION(CInventoryOwner, CGameObject, cast_inventory_owner)
DECLARE_SPECIALIZATION(CActor, CGameObject, cast_actor)
DECLARE_SPECIALIZATION(CGameObject, CInventoryOwner, cast_game_object)
DECLARE_SPECIALIZATION(CWeapon, CInventoryItem, cast_weapon)
DECLARE_SPECIALIZATION(CWeapon, CGameObject, cast_weapon)
DECLARE_SPECIALIZATION(CFoodItem, CInventoryItem, cast_food_item)
DECLARE_SPECIALIZATION(CMissile, CInventoryItem, cast_missile)
DECLARE_SPECIALIZATION(CFlashlight, CInventoryItem, cast_flashlight)
DECLARE_SPECIALIZATION(CCustomZone, CGameObject, cast_custom_zone)
DECLARE_SPECIALIZATION(CWeaponMagazined, CWeapon, cast_weapon_magazined)
DECLARE_SPECIALIZATION(CHudItem, CInventoryItem, cast_hud_item)
DECLARE_SPECIALIZATION(CPhysicsShellHolder, CGameObject, cast_physics_shell_holder)
DECLARE_SPECIALIZATION(IInputReceiver, CGameObject, cast_input_receiver)
DECLARE_SPECIALIZATION(CWeaponAmmo, CInventoryItem, cast_weapon_ammo)
DECLARE_SPECIALIZATION(CParticlesPlayer, CGameObject, cast_particles_player)
DECLARE_SPECIALIZATION(CArtefact, CGameObject, cast_artefact)
DECLARE_SPECIALIZATION(CCustomMonster, CGameObject, cast_custom_monster)
DECLARE_SPECIALIZATION(CAI_Stalker, CGameObject, cast_stalker)
DECLARE_SPECIALIZATION(CScriptEntity, CGameObject, cast_script_entity)
DECLARE_SPECIALIZATION(CSpaceRestrictor, CGameObject, cast_restrictor)
DECLARE_SPECIALIZATION(CExplosive, CGameObject, cast_explosive)
DECLARE_SPECIALIZATION(CGameObject, CAttachmentOwner, cast_game_object)
DECLARE_SPECIALIZATION(CGameObject, CInventoryItem, cast_game_object)
DECLARE_SPECIALIZATION(CAttachableItem, CGameObject, cast_attachable_item)
DECLARE_SPECIALIZATION(CHolderCustom, CGameObject, cast_holder_custom)
DECLARE_SPECIALIZATION(CAttachmentOwner, CGameObject, cast_attachment_owner)
DECLARE_SPECIALIZATION(CEatableItem, CInventoryItem, cast_eatable_item)
DECLARE_SPECIALIZATION(CBaseMonster, CGameObject, cast_base_monster)
DECLARE_SPECIALIZATION(CSE_Abstract, CSE_ALifeInventoryItem, cast_abstract)
DECLARE_SPECIALIZATION(CSE_Abstract, CSE_ALifeTraderAbstract, cast_abstract)
DECLARE_SPECIALIZATION(CSE_Abstract, CSE_ALifeGroupAbstract, cast_abstract)
DECLARE_SPECIALIZATION(CSE_Abstract, CSE_ALifeSchedulable, cast_abstract)
DECLARE_SPECIALIZATION(CSE_ALifeGroupAbstract, CSE_Abstract, cast_group_abstract)
DECLARE_SPECIALIZATION(CSE_ALifeSchedulable, CSE_Abstract, cast_schedulable)
DECLARE_SPECIALIZATION(CSE_ALifeInventoryItem, CSE_Abstract, cast_inventory_item)
DECLARE_SPECIALIZATION(CSE_ALifeTraderAbstract, CSE_Abstract, cast_trader_abstract)
DECLARE_SPECIALIZATION(CSE_Visual, CSE_Abstract, visual)
DECLARE_SPECIALIZATION(CSE_Motion, CSE_Abstract, motion)
DECLARE_SPECIALIZATION(ISE_Shape, CSE_Abstract, shape)
DECLARE_SPECIALIZATION(CSE_Abstract, CSE_PHSkeleton, cast_abstract)
DECLARE_SPECIALIZATION(CSE_ALifeObject, CSE_Abstract, cast_alife_object)
DECLARE_SPECIALIZATION(CSE_ALifeDynamicObject, CSE_Abstract, cast_alife_dynamic_object)
DECLARE_SPECIALIZATION(CSE_ALifeItemAmmo, CSE_Abstract, cast_item_ammo)
DECLARE_SPECIALIZATION(CSE_ALifeItemWeapon, CSE_Abstract, cast_item_weapon)
DECLARE_SPECIALIZATION(CSE_ALifeItemDetector, CSE_Abstract, cast_item_detector)
DECLARE_SPECIALIZATION(CSE_ALifeMonsterAbstract, CSE_Abstract, cast_monster_abstract)
DECLARE_SPECIALIZATION(CSE_ALifeHumanAbstract, CSE_Abstract, cast_human_abstract)
DECLARE_SPECIALIZATION(CSE_ALifeAnomalousZone, CSE_Abstract, cast_anomalous_zone)
DECLARE_SPECIALIZATION(CSE_ALifeTrader, CSE_Abstract, cast_trader)
DECLARE_SPECIALIZATION(CSE_ALifeCreatureAbstract, CSE_Abstract, cast_creature_abstract)
DECLARE_SPECIALIZATION(CSE_ALifeSmartZone, CSE_Abstract, cast_smart_zone)
DECLARE_SPECIALIZATION(CSE_ALifeOnlineOfflineGroup, CSE_Abstract, cast_online_offline_group)
DECLARE_SPECIALIZATION(CSE_ALifeItemPDA, CSE_Abstract, cast_item_pda)

template<> 
struct has_dcast<CGameObject*, CObject*> : std::true_type {
    static CGameObject* cast(CObject* ptr) {
		return static_cast<CGameObject*>(ptr);
    } 
};

// Additional specializations
DECLARE_SPECIALIZATION(CGrenade, CInventoryItem, cast_grenade)
DECLARE_SPECIALIZATION(CCustomOutfit, CInventoryItem, cast_outfit)
DECLARE_SPECIALIZATION(CArtefact, CInventoryItem, cast_artefact)
DECLARE_SPECIALIZATION(CHelmet, CInventoryItem, cast_helmet)
DECLARE_SPECIALIZATION(CTorch, CInventoryItem, cast_torch)
DECLARE_SPECIALIZATION(CAttachableItem, CInventoryItem, cast_attachable_item)
DECLARE_SPECIALIZATION(CPda, CInventoryItem, cast_pda)
DECLARE_SPECIALIZATION(CCustomDevice, CInventoryItem, cast_custom_device)
DECLARE_SPECIALIZATION(CCustomDetector, CInventoryItem, cast_custom_detector)
DECLARE_SPECIALIZATION(CFlare, CInventoryItem, cast_flare)
DECLARE_SPECIALIZATION(CWeaponMagazined, CInventoryItem, cast_weapon_magazined)
DECLARE_SPECIALIZATION(CWeaponKnife, CInventoryItem, cast_weapon_knife)
DECLARE_SPECIALIZATION(CWeaponBinoculars, CInventoryItem, cast_weapon_binoculars)
DECLARE_SPECIALIZATION(CWeaponMagazinedWGrenade, CInventoryItem, cast_weapon_magazined_w_grenade)
DECLARE_SPECIALIZATION(CWeaponBM16, CInventoryItem, cast_weapon_bm16)
DECLARE_SPECIALIZATION(CWeaponRG6, CInventoryItem, cast_weapon_rg6)
DECLARE_SPECIALIZATION(CWeaponRPG7, CInventoryItem, cast_weapon_rpg7)
DECLARE_SPECIALIZATION(CBolt, CInventoryItem, cast_bolt)
DECLARE_SPECIALIZATION(CSilencer, CInventoryItem, cast_addon_silencer)
DECLARE_SPECIALIZATION(CScope, CInventoryItem, cast_addon_scope)
DECLARE_SPECIALIZATION(CGrenadeLauncher, CInventoryItem, cast_addon_grenade_launcher)

DECLARE_SPECIALIZATION(CEntityAlive, CInventoryOwner, cast_entity_alive)
DECLARE_SPECIALIZATION(CActor, CInventoryOwner, cast_actor)

DECLARE_SPECIALIZATION(CInventoryOwner, CEntityAlive, cast_inventory_owner)

DECLARE_SPECIALIZATION(CWeaponKnife, CWeapon, cast_weapon_knife)
DECLARE_SPECIALIZATION(CWeaponBinoculars, CWeapon, cast_weapon_binoculars)
DECLARE_SPECIALIZATION(CWeaponMagazinedWGrenade, CWeapon, cast_weapon_magazined_w_grenade)
DECLARE_SPECIALIZATION(CWeaponBM16, CWeapon, cast_weapon_bm16)
DECLARE_SPECIALIZATION(CWeaponRG6, CWeapon, cast_weapon_rg6)
DECLARE_SPECIALIZATION(CWeaponRPG7, CWeapon, cast_weapon_rpg7)

DECLARE_SPECIALIZATION(CGameObject, CActor, cast_game_object)

DECLARE_SPECIALIZATION(CWeaponStatMgun, CHolderCustom, cast_weapon_stat_mgun)
DECLARE_SPECIALIZATION(CCar, CHolderCustom, cast_car)
DECLARE_SPECIALIZATION(CGameObject, CHolderCustom, cast_game_object)

DECLARE_SPECIALIZATION(CHolderCustom, CObject, cast_holder_custom)
DECLARE_SPECIALIZATION(CPhysicsShellHolder, CObject, cast_physics_shell_holder)
DECLARE_SPECIALIZATION(CActor, CObject, cast_actor)
DECLARE_SPECIALIZATION(CInventoryOwner, CObject, cast_inventory_owner)
DECLARE_SPECIALIZATION(CCar, CObject, cast_car)
DECLARE_SPECIALIZATION(CSpectator, CObject, cast_spectator)

DECLARE_SPECIALIZATION(CHolderCustom, CPhysicsShellHolder, cast_holder_custom)
DECLARE_SPECIALIZATION(CExplosiveRocket, CPhysicsShellHolder, cast_explosive_rocket)
DECLARE_SPECIALIZATION(CGrenade, CPhysicsShellHolder, cast_grenade)

DECLARE_SPECIALIZATION(CInventoryBox, CGameObject, cast_inventory_box)
DECLARE_SPECIALIZATION(CHangingLamp, CGameObject, cast_hanging_lamp)
DECLARE_SPECIALIZATION(CHelicopter, CGameObject, cast_helicopter)
DECLARE_SPECIALIZATION(CPhantom, CGameObject, cast_phantom)
DECLARE_SPECIALIZATION(CScriptZone, CGameObject, cast_script_zone)
DECLARE_SPECIALIZATION(CHudItem, CGameObject, cast_hud_item)
DECLARE_SPECIALIZATION(CLevelChanger, CGameObject, cast_level_changer)
DECLARE_SPECIALIZATION(CPhysicObject, CGameObject, cast_physics_object)
DECLARE_SPECIALIZATION(CPda, CGameObject, cast_pda)
DECLARE_SPECIALIZATION(CCustomDevice, CGameObject, cast_custom_device)
DECLARE_SPECIALIZATION(CWeaponAmmo, CGameObject, cast_weapon_ammo)
DECLARE_SPECIALIZATION(CUsableScriptObject, CGameObject, cast_usable_script_object)
DECLARE_SPECIALIZATION(CProjector, CGameObject, cast_projector)
DECLARE_SPECIALIZATION(CAI_Trader, CGameObject, cast_trader)

DECLARE_SPECIALIZATION(CBolt, CMissile, cast_bolt)
DECLARE_SPECIALIZATION(CGrenade, CMissile, cast_grenade)

DECLARE_SPECIALIZATION(CExplosiveRocket, CCustomRocket, cast_explosive_rocket)

#endif

#ifdef XRPHYSICS_EXPORTS
#include "../xrCDB/ISpatial.h"

DECLARE_SPECIALIZATION(IRenderable, ISpatial, dcast_Renderable)
DECLARE_SPECIALIZATION(IRender_Light, ISpatial, dcast_Light)
DECLARE_SPECIALIZATION(CObject, ISpatial, dcast_CObject)
#endif

#if defined(XRRENDER_R1_EXPORTS) || defined(XRRENDER_R2_EXPORTS) || defined(XRRENDER_R3_EXPORTS) || defined(XRRENDER_R4_EXPORTS)
#include "../xrCDB/ISpatial.h"

DECLARE_SPECIALIZATION(IRenderable, ISpatial, dcast_Renderable)
DECLARE_SPECIALIZATION(IRender_Light, ISpatial, dcast_Light)
DECLARE_SPECIALIZATION(CObject, ISpatial, dcast_CObject)
#endif

#undef DECLARE_SPECIALIZATION

template < typename _Ty >
using clean_type_t = typename std::remove_cv_t<std::remove_reference_t<std::remove_pointer_t< _Ty >>>;

#pragma warning(push)
#pragma warning(disable : 4702)
template < typename _To, typename _From >
__forceinline _To smart_cast(_From* ptr)
{
    // Try dedicated cast function if available
    if constexpr (has_dcast<_To, _From*>::value)
        return has_dcast<_To, _From*>::cast(ptr);
    else
	    return fast_dynamic_cast<_To>(ptr);
};
#pragma warning(pop)

// const T*
template < typename _To, typename _From >
__forceinline const _To smart_cast(const _From* ptr)
{
    _From* nonconst_ptr = const_cast<_From*>(ptr);
    _To casted_ptr = smart_cast<_To>(nonconst_ptr);
    return const_cast<const _To>(casted_ptr);
};

// T&
template < typename _To, typename _From, typename = std::enable_if_t<!std::is_same<clean_type_t<_To>, clean_type_t<_From&>>::value>>
__forceinline _To smart_cast(_From& ref)
{
    using _ToPtr = std::add_pointer_t<std::remove_reference_t<_To>>;
    auto casted_ptr = smart_cast<_ToPtr>(&ref);
    if (!casted_ptr)
        throw std::bad_cast{};
    return *casted_ptr;
};

// const T&
template < typename _To, typename _From, typename = std::enable_if_t<!std::is_same<clean_type_t<_To>, clean_type_t<_From&>>::value>>
__forceinline _To smart_cast(const _From& ref)
{
    using _ToPtr = std::add_pointer_t<std::remove_reference_t<_To>>;
    auto casted_ptr = smart_cast<_ToPtr>(const_cast<_From*>(&ref));
    if (!casted_ptr)
        throw std::bad_cast{};
    return *casted_ptr;
};

// T -> T
template < typename _To >
__forceinline _To smart_cast(_To ptr) { return ptr; }
