#include "stdafx.h"
#include "phmovementcontrol.h"

#include "../xrEngine/cl_intersect.h"
#include "../xrserverentities/alife_space.h"

#include "../xrphysics/phCharacter.h"
#include "../xrphysics/IPHCapture.h"
#include "../xrphysics/iphysicsshellholder.h"
#include "../xrphysics/ElevatorState.h"
#include "../xrphysics/CalculateTriangle.h"
#include "../xrphysics/Icolisiondamageinfo.h"
#include "../xrphysics/phvalide.h"
#include "../xrphysics/physicsshell.h"
#include "../xrphysics/iphworld.h"
#include "../xrEngine/gamemtllib.h"
#include "../xrEngine/xr_object.h"
#include "CaptureBoneCallback.h"
#include "Level.h"
#include "physicsshellholder.h"
#ifdef DEBUG
#include "phdebug.h"
#endif

#include "../Include/xrRender/Kinematics.h"

#define GROUND_FRICTION	10.0f
#define AIR_FRICTION	0.01f
#define WALL_FRICTION	3.0f
//#define AIR_RESIST		0.001f

#define def_X_SIZE_2	0.35f
#define def_Y_SIZE_2	0.8f
#define def_Z_SIZE_2	0.35f

const u64 after_creation_collision_hit_block_steps_number=100;

CPHMovementControl::CPHMovementControl(CObject* parent)
{
	pObject=parent;

#ifdef DEBUG
	if(debug_output().ph_dbg_draw_mask1().test(ph_m1_DbgTrackObject)&&(!!pObject->cName())&&stricmp(debug_output().PH_DBG_ObjectTrackName(),*pObject->cName())==0)
	{
		Msg("CPHMovementControl::CPHMovementControl %s (constructor) %f,%f,%pObjectf",debug_output().PH_DBG_ObjectTrackName(),pObject->Position().x,pObject->Position().y,pObject->Position().z);
	}

#endif

	m_material			=0;
	m_capture			=NULL;
	b_exect_position	=true;
	m_start_index		=0;
	eOldEnvironment =	peInAir;
	eEnvironment =		peInAir;
	aabb.set			(-def_X_SIZE_2,0,-def_Z_SIZE_2, def_X_SIZE_2, def_Y_SIZE_2*2, def_Z_SIZE_2);
	///vFootCenter.set		(0,0,0);
	//vFootExt.set		(0,0,0);
	fMass				= 100;
	fMinCrashSpeed		= 12.0f;
	fMaxCrashSpeed		= 25.0f;
	vVelocity.set		(0,0,0);
	vPosition.set		(0,0,0);
	vExternalImpulse.set(0,0,0);
	bExernalImpulse		=false;
	fLastMotionMag		= 1.f;
	SetPathDir			(Fvector().set(0,0,1));
	//fAirFriction		= AIR_FRICTION;
	//fWallFriction		= WALL_FRICTION;
	//fGroundFriction		= GROUND_FRICTION;
	//fFriction			= fAirFriction;
	bIsAffectedByGravity= TRUE;
	fActualVelocity		= 0;
	m_fGroundDelayFactor= 1.f;
	gcontact_HealthLost = 0;

	fContactSpeed		=	0.f;
	fAirControlParam	=	0.f;
	m_character			=	NULL;
	m_dwCurBox			=	0xffffffff;
	fCollisionDamageFactor=1.f;
	in_dead_area_count	=0;
	bNonInteractiveMode =false;
	block_damage_step_end = u64(-1);
}



CPHMovementControl::~CPHMovementControl(void)
{
	if(m_character)
		m_character->Destroy();
	DeleteCharacterObject();
	phcapture_destroy(m_capture);
}



static ALife::EHitType	 DefineCollisionHitType	( u16 material_idx )	
{
	return ALife::eHitTypeStrike;
}

//static Fvector old_pos={0,0,0};
//static bool bFirst=true;
void CPHMovementControl::AddControlVel	(const Fvector& vel)
{
	vExternalImpulse.add(vel);
	bExernalImpulse=true;
}
void CPHMovementControl::ApplyImpulse(const Fvector& dir,const float P)
{
	VERIFY( m_character );
	if(fis_zero(P))
		return;
	Fvector force;
	force.set(dir);
	force.mul(P/fixed_step);
	
	AddControlVel(force);
 	m_character->ApplyImpulse(dir,P);
}
void CPHMovementControl::SetVelocityLimit(float val)
{
	if(m_character)
		m_character->SetMaximumVelocity(val);
}
float CPHMovementControl::VelocityLimit()
{
	if(!m_character || !m_character->b_exist) return 0.f;
	return m_character->GetMaximumVelocity();
}

void CPHMovementControl::in_shedule_Update(u32 DT)
{
	
	if(!m_capture)
		return;
	if(m_capture->Failed())
				phcapture_destroy(m_capture);
}

void CPHMovementControl::Calculate(Fvector& vAccel,const Fvector& camDir,float /**ang_speed/**/,float jump,float /**dt/**/,bool /**bLight/**/)
{
	Fvector previous_position;previous_position.set(vPosition);
	m_character->IPosition(vPosition);
	if(bExernalImpulse)
	{
		
		vAccel.add(vExternalImpulse);
		m_character->ApplyForce(vExternalImpulse);
		vExternalImpulse.set(0.f,0.f,0.f);
		
		bExernalImpulse=false;
	}
	//vAccel.y=jump;
	float mAccel=vAccel.magnitude();
	m_character->SetCamDir(camDir);
	m_character->SetMaximumVelocity(mAccel/10.f);
	//if(!fis_zero(mAccel))vAccel.mul(1.f/mAccel);
	m_character->SetAcceleration(vAccel);
	if(!fis_zero(jump)) m_character->Jump(vAccel);
	
	m_character->GetSavedVelocity(vVelocity); 
	fActualVelocity=vVelocity.magnitude();
	//Msg("saved avel %f", fActualVelocity);
	gcontact_Was=m_character->ContactWas();

//////

	UpdateCollisionDamage( );

/*
	u16 mat_injurios = m_character->InjuriousMaterialIDX();

	if(m_character->LastMaterialIDX()!=GAMEMTL_NONE_IDX)
	{
		const SGameMtl *last_material=GMLib.GetMaterialByIdx(m_character->LastMaterialIDX());
		if( last_material->Flags.test(SGameMtl::flInjurious) )
			mat_injurios = m_character->LastMaterialIDX();
	}

	if( mat_injurios!=GAMEMTL_NONE_IDX)
	{
		if( fis_zero(gcontact_HealthLost) )
				m_character->SetHitType( DefineCollisionHitType( mat_injurios ) );
		gcontact_HealthLost+=Device.fTimeDelta*GMLib.GetMaterialByIdx( mat_injurios )->fInjuriousSpeed;
	}

*/
	//IPhysicsShellHolder * O=di->DamageObject();
	//SCollisionHitCallback* cc= O ? O->get_collision_hit_callback() : NULL;
	ICollisionDamageInfo	*cdi=CollisionDamageInfo();
	if(cdi->HitCallback())
			cdi->HitCallback()->call((m_character->PhysicsRefObject()),fMinCrashSpeed,fMaxCrashSpeed,fContactSpeed,gcontact_HealthLost,CollisionDamageInfo());
		
////////

	TraceBorder(previous_position);
	CheckEnvironment(vPosition);
	bSleep=false;
	m_character->Reinit();
}
void CPHMovementControl::UpdateCollisionDamage( )
{
//reset old
	fContactSpeed				= 0.f;
	gcontact_HealthLost			= 0;
	gcontact_Power				= 0;
	const ICollisionDamageInfo* di=m_character->CollisionDamageInfo();
 	fContactSpeed=di->ContactVelocity();

	if(block_damage_step_end!=u64(-1))
	{
		if( physics_world()->StepsNum() < block_damage_step_end )
		{
			fContactSpeed = 0.f;
			return;
		} else 
			block_damage_step_end = u64(-1);
	}

// calc new
	gcontact_Power				= fContactSpeed/fMaxCrashSpeed;
	if (fContactSpeed>fMinCrashSpeed) 
	{
			gcontact_HealthLost = 
			((fContactSpeed-fMinCrashSpeed))/(fMaxCrashSpeed-fMinCrashSpeed);
			VERIFY( m_character );
			m_character->SetHitType( DefineCollisionHitType( m_character->LastMaterialIDX() ) );
	}



	//const ICollisionDamageInfo* di=m_character->CollisionDamageInfo();
	//fContactSpeed=0.f;
	//{
	//	fContactSpeed=di->ContactVelocity();
	//	gcontact_Power				= fContactSpeed/fMaxCrashSpeed;
	//	gcontact_HealthLost			= 0;
	//	if (fContactSpeed>fMinCrashSpeed) 
	//	{
	//		gcontact_HealthLost = 
	//			((fContactSpeed-fMinCrashSpeed))/(fMaxCrashSpeed-fMinCrashSpeed);
	//		m_character->SetHitType( DefineCollisionHitType( m_character->LastMaterialIDX() ) );
	//	}
	//}
}

#include "../xrCore/_vector3d_ext.h"

void CPHMovementControl::SetActorRestrictorRadius(ERestrictionType rt,float r)
{
	if(m_character&&eCharacterType==actor)
		(m_character)->SetRestrictorRadius(rt,r);
		//static_cast<CPHActorCharacter*>(m_character)->SetRestrictorRadius(rt,r);
}

void CPHMovementControl::Load					(LPCSTR section){

	//capture
	
	//xr_strcpy(m_capture_bone,pSettings->r_string(section,"capture_bone"));
	
	Fbox	bb;

	// m_PhysicMovementControl: BOX
	Fvector	vBOX1_center= pSettings->r_fvector3	(section,"ph_box1_center"	);
	Fvector	vBOX1_size	= pSettings->r_fvector3	(section,"ph_box1_size"		);
	bb.set	(vBOX1_center,vBOX1_center); bb.grow(vBOX1_size);
	SetBox		(1,bb);

	// m_PhysicMovementControl: BOX
	Fvector	vBOX0_center= pSettings->r_fvector3	(section,"ph_box0_center"	);
	Fvector	vBOX0_size	= pSettings->r_fvector3	(section,"ph_box0_size"		);
	bb.set	(vBOX0_center,vBOX0_center); bb.grow(vBOX0_size);
	SetBox		(0,bb);

	//// m_PhysicMovementControl: Foots
	//Fvector	vFOOT_center= pSettings->r_fvector3	(section,"ph_foot_center"	);
	//Fvector	vFOOT_size	= pSettings->r_fvector3	(section,"ph_foot_size"		);
	//bb.set	(vFOOT_center,vFOOT_center); bb.grow(vFOOT_size);
	//SetFoots	(vFOOT_center,vFOOT_size);

	// m_PhysicMovementControl: Crash speed and mass
	float	cs_min		= pSettings->r_float	(section,"ph_crash_speed_min"	);
	float	cs_max		= pSettings->r_float	(section,"ph_crash_speed_max"	);
	float	mass		= pSettings->r_float	(section,"ph_mass"				);
	xr_token retrictor_types[]={
		{ "actor",			rtActor},
		{ "medium_monster",	rtMonsterMedium},
		{ "stalker",		rtStalker	},
		{ "none",			rtNone	},
		{ 0,							0}
	};

	if(pSettings->line_exist(section,"actor_restrictor"))
		SetRestrictionType(ERestrictionType(pSettings->r_token(section,"actor_restrictor",retrictor_types)));
	fCollisionDamageFactor=READ_IF_EXISTS(pSettings,r_float,section,"ph_collision_damage_factor",fCollisionDamageFactor);
	R_ASSERT3(fCollisionDamageFactor<=1.f,"ph_collision_damage_factor >1.",section);
	SetCrashSpeeds	(cs_min,cs_max);
	SetMass		(mass);


	// m_PhysicMovementControl: Frictions
	//float af, gf, wf;
	//af					= pSettings->r_float	(section,"ph_friction_air"	);
	//gf					= pSettings->r_float	(section,"ph_friction_ground");
	//wf					= pSettings->r_float	(section,"ph_friction_wall"	);
	//SetFriction	(af,wf,gf);

	// BOX activate
//	ActivateBox	(0);
}

void CPHMovementControl::CheckEnvironment(const Fvector &/**V/**/){
	eOldEnvironment=eEnvironment;
	switch (m_character->CheckInvironment()){
case peOnGround : eEnvironment=peOnGround;break;
case peInAir :		eEnvironment=peInAir		;break;
case peAtWall : eEnvironment=peAtWall		;break;
	}
}

void CPHMovementControl::GroundNormal(Fvector & norm)		
{
if(m_character&&m_character->b_exist)m_character->GroundNormal(norm);
else norm.set(0.f,1.f,0.f);

}

void	CPHMovementControl::SetEnvironment( int enviroment,int old_enviroment){
	switch(enviroment){
	case 0: eEnvironment=peOnGround;
		break;
	case 1: eEnvironment=peAtWall;
		break;
	case 2: eEnvironment=peInAir;
	}
	switch(old_enviroment){
	case 0: eOldEnvironment=peOnGround;
		break;
	case 1: eOldEnvironment=peAtWall;
		break;
	case 2: eOldEnvironment=peInAir;
	}
}
void	CPHMovementControl::SetPosition(const Fvector &P){	
#ifdef DEBUG
	if(ph_dbg_draw_mask1.test(ph_m1_DbgTrackObject)&&(!!pObject->cName())&&stricmp(PH_DBG_ObjectTrackName(),*pObject->cName())==0)
	{
		Msg("CPHMovementControl::SetPosition %s (Object Position) %f,%f,%f",PH_DBG_ObjectTrackName(),pObject->Position().x,pObject->Position().y,pObject->Position().z);
		Msg("CPHMovementControl::SetPosition %s (CPHMovementControl::vPosition) %f,%f,%f",PH_DBG_ObjectTrackName(),vPosition.x,vPosition.y,vPosition.z);
	}
#endif
	vPosition.set	(P);  VERIFY( m_character ) ;m_character->SetPosition(vPosition);

}
bool		CPHMovementControl::		TryPosition				(Fvector& pos)															
{

VERIFY_BOUNDARIES2(pos,ph_boundaries(),m_character->PhysicsRefObject(),"CPHMovementControl::TryPosition	arqument pos");

#ifdef DEBUG
		if(ph_dbg_draw_mask1.test(ph_m1_DbgTrackObject)&&(!!pObject->cName())&&stricmp(PH_DBG_ObjectTrackName(),*pObject->cName())==0)
		{
			Msg("CPHMovementControl::TryPosition %s (Object Position) %f,%f,%f",PH_DBG_ObjectTrackName(),pObject->Position().x,pObject->Position().y,pObject->Position().z);
			Msg("CPHMovementControl::TryPosition %s (CPHMovementControl::vPosition) %f,%f,%f",PH_DBG_ObjectTrackName(),vPosition.x,vPosition.y,vPosition.z);
		}
#endif
		if (m_character->b_exist) {	
			bool					ret = m_character->TryPosition(pos,b_exect_position)&&!bExernalImpulse;
			m_character->GetPosition(vPosition);
			return					(ret);
		}

		vPosition.set	(pos);
		return			(true);
}


void		CPHMovementControl::GetPosition		(Fvector &P)
{	
VERIFY_BOUNDARIES2(P,ph_boundaries(),m_character->PhysicsRefObject(),"CPHMovementControl::GetPosition	arqument pos");

#ifdef DEBUG
	if(ph_dbg_draw_mask1.test(ph_m1_DbgTrackObject)&&(!!pObject->cName())&&stricmp(PH_DBG_ObjectTrackName(),*pObject->cName())==0)
	{
		Msg("CPHMovementControl::GetPosition %s (Object Position) %f,%f,%f",PH_DBG_ObjectTrackName(),pObject->Position().x,pObject->Position().y,pObject->Position().z);
		Msg("CPHMovementControl::GetPosition %s (CPHMovementControl::vPosition) %f,%f,%f",PH_DBG_ObjectTrackName(),vPosition.x,vPosition.y,vPosition.z);
	}
#endif
	P.set			(vPosition); 
VERIFY_BOUNDARIES2(vPosition, ph_boundaries(), m_character->PhysicsRefObject(),"CPHMovementControl::GetPosition	out pos");
}

void	CPHMovementControl::AllocateCharacterObject(CharacterType type)
{
	switch(type)
	{
		case actor:		m_character = create_actor_character( false )				;	break;
		//case actor:	m_character = xr_new<CPHActorCharacter>	()					;	break;
		//case ai:		m_character = xr_new<CPHAICharacter>	()					;	break;
		case ai:		m_character = create_ai_character()							;	break;
		default:		NODEFAULT;
	}
	eCharacterType=type;
	m_character->SetMas(fMass);
	m_character->SetPosition(vPosition);
#ifdef DEBUG
	if(ph_dbg_draw_mask1.test(ph_m1_DbgTrackObject)&&(!!pObject->cName())&&stricmp(PH_DBG_ObjectTrackName(),*pObject->cName())==0)
	{
		Msg("CPHMovementControl::AllocateCharacterObject %s (Object Position) %f,%f,%f",PH_DBG_ObjectTrackName(),pObject->Position().x,pObject->Position().y,pObject->Position().z);
		Msg("CPHMovementControl::AllocateCharacterObject %s (CPHMovementControl::vPosition) %f,%f,%f",PH_DBG_ObjectTrackName(),vPosition.x,vPosition.y,vPosition.z);
	}
#endif
}

void	CPHMovementControl::PHCaptureObject(CPhysicsShellHolder* object, CPHCaptureBoneCallback* cb /*=0*/ )
{
if(m_capture)
		return;
if( !object||
	!object->PPhysicsShell()||
	!object->m_pPhysicsShell->isActive()
	)
	return;

m_capture = phcapture_create( m_character, object,static_cast<NearestToPointCallback*>( cb ) );

//m_capture=xr_new<CPHCapture>(m_character,
//							 object,
//							 cb
//							 );
}

void	CPHMovementControl::PHCaptureObject(CPhysicsShellHolder* object,u16 element)
{
	if(m_capture) 
		return;

	if( !object||
	    !object->PPhysicsShell()||
		!object->PPhysicsShell()->isActive()
		)
		return;

	//m_capture=xr_new<CPHCapture>(m_character,
	//	object,
	//	element
	//	);
	m_capture = phcapture_create( m_character, object, element ); 
}

Fvector CPHMovementControl::PHCaptureGetNearestElemPos(const CPhysicsShellHolder* object)
{
	R_ASSERT3((object->m_pPhysicsShell != NULL), "NO Phisics Shell for object ", *object->cName());

	CPhysicsElement *ph_elem =  object->m_pPhysicsShell->NearestToPoint(vPosition);

	Fvector v;
	ph_elem->GetGlobalPositionDynamic(&v);	
	
	return v;
}

Fmatrix CPHMovementControl::PHCaptureGetNearestElemTransform(CPhysicsShellHolder* object)
{
	CPhysicsElement *ph_elem =  object->m_pPhysicsShell->NearestToPoint(vPosition);

	Fmatrix m;
	ph_elem->GetGlobalTransformDynamic(&m);

	return m;
}


void CPHMovementControl::PHReleaseObject()
{
	if(m_capture) 
		m_capture->Release();
}

void	CPHMovementControl::DestroyCharacter()
{
	VERIFY( m_character );
	m_character->Destroy();
	phcapture_destroy(m_capture);
	//xr_delete(m_capture);
	//xr_delete<CPHSimpleCharacter>(m_character);
}

void	CPHMovementControl::DeleteCharacterObject()
{
	xr_delete(m_character);
	phcapture_destroy(m_capture);
}

void CPHMovementControl::JumpV(const Fvector &jump_velocity)
{
	m_character->Enable();
	m_character->Jump(jump_velocity);
}

void CPHMovementControl::Jump(const Fvector &end_point, float time)
{
	//vPosition
	Jump(smart_cast<CGameObject*>(m_character->PhysicsRefObject())->Position(),end_point,time);
}



void CPHMovementControl::Jump(const Fvector &start_point,const Fvector &end_point, float time)
{
	Fvector velosity;
	velosity.sub(end_point,start_point);
	TransferenceToThrowVel(velosity,time,physics_world()->Gravity());
	JumpV(velosity);
}
float CPHMovementControl::Jump(const Fvector &end_point)
{
	float time =JumpMinVelTime(end_point);
	Jump(smart_cast<CGameObject*>(m_character->PhysicsRefObject())->Position(),end_point,time);
	return time;
}
void CPHMovementControl::GetJumpMinVelParam(Fvector &min_vel,float &time,JumpType &type,const Fvector &end_point)
{
	time =JumpMinVelTime(end_point);
	GetJumpParam(min_vel,type,end_point,time);
}

float CPHMovementControl::JumpMinVelTime(const Fvector &end_point)
{
	return ThrowMinVelTime(Fvector().sub(end_point,smart_cast<CGameObject*>(m_character->PhysicsRefObject())->Position()),physics_world()->Gravity());
}

void CPHMovementControl::GetJumpParam(Fvector &velocity, JumpType &type,const Fvector &end_point, float time)
{
	Fvector velosity;velosity.sub(smart_cast<CGameObject*>(m_character->PhysicsRefObject())->Position(),end_point);
	TransferenceToThrowVel(velosity,time,physics_world()->Gravity());
	if(velocity.y<0.f)
	{
		type=jtStrait;
		return;
	}
	float rise_time=velosity.y/physics_world()->Gravity();
	if(_abs(rise_time-time)<EPS_L)
	{
		type=jtHigh;
	}
	else if(rise_time>time)
	{
		type=jtStrait;
	}
	else
	{
		type=jtCurved;
	}

}

void CPHMovementControl::SetMaterial(u16 material)
{
	m_material=material;
	if(m_character)
	{
		m_character->SetMaterial(material);
	}
}
void CPHMovementControl::CreateCharacter()
{	
	dVector3 size={aabb.x2-aabb.x1,aabb.y2-aabb.y1,aabb.z2-aabb.z1};
	m_character->Create(size);
	m_character->SetMaterial(m_material);
	m_character->SetAirControlFactor(fAirControlParam);
#ifdef DEBUG
	if(ph_dbg_draw_mask1.test(ph_m1_DbgTrackObject)&&(!!pObject->cName())&&stricmp(PH_DBG_ObjectTrackName(),*pObject->cName())==0)
	{
		Msg("CPHMovementControl::CreateCharacter %s (Object Position) %f,%f,%f",PH_DBG_ObjectTrackName(),pObject->Position().x,pObject->Position().y,pObject->Position().z);
		Msg("CPHMovementControl::CreateCharacter %s (CPHMovementControl::vPosition) %f,%f,%f",PH_DBG_ObjectTrackName(),vPosition.x,vPosition.y,vPosition.z);
	}
#endif
	m_character->SetPosition(vPosition);
	m_character->SetCollisionDamageFactor(fCollisionDamageFactor*fCollisionDamageFactor);
	trying_times[0]=trying_times[1]=trying_times[2]=trying_times[3]=u32(-1);
	trying_poses[0].set(vPosition);
	trying_poses[1].set(vPosition);
	trying_poses[2].set(vPosition);
	trying_poses[3].set(vPosition);
}
CPHSynchronize*	CPHMovementControl::GetSyncItem()
{
	if(m_character)	return smart_cast<CPHSynchronize*>(m_character);
	else			return 0;
}
void CPHMovementControl::Freeze()
{
	if(m_character)m_character->Freeze();
}
void CPHMovementControl::UnFreeze()
{
	if(m_character) m_character->UnFreeze();
}

void CPHMovementControl::ActivateBox		(DWORD id, BOOL Check/*false*/)	
{ 
	if (Check && (m_dwCurBox == id)) return;
	m_dwCurBox = id;
	aabb.set(boxes[id]);
	if(!m_character||!m_character->b_exist) return;
	dVector3 size={aabb.x2-aabb.x1,aabb.y2-aabb.y1,aabb.z2-aabb.z1};
	m_character->SetBox(size);
	//Fvector v;
	//m_character->GetVelocity(v);
	//m_character->Destroy();
	//CreateCharacter();	
	//m_character->SetVelocity(v);
	//m_character->SetPosition(vPosition);	
}
void CPHMovementControl::InterpolateBox		(DWORD id, float k)
{ 
	if (m_dwCurBox == id) return;
	if(!m_character||!m_character->b_exist) return;
	dVector3 size={aabb.x2-aabb.x1,aabb.y2-aabb.y1,aabb.z2-aabb.z1};
	dVector3 to_size={boxes[id].x2-boxes[id].x1,boxes[id].y2-boxes[id].y1,boxes[id].z2-boxes[id].z1};
	dVectorInterpolate(size,to_size,k);
	m_character->SetBox(size);
}

void CPHMovementControl::ApplyHit(const Fvector& dir,const float P,ALife::EHitType hit_type)
{

	VERIFY( m_character );
	// stop-motion
	if(!m_character->CastActorCharacter())
		return;
	if ( 	
		(Environment()==CPHMovementControl::peOnGround || Environment()==CPHMovementControl::peAtWall) 
		)
	{
		switch(hit_type)
		{
			case ALife::eHitTypeBurn  :												;//stop
			case ALife::eHitTypeShock :												;//stop
			case ALife::eHitTypeStrike:												;//stop
			case ALife::eHitTypeWound:			SetVelocity(Fvector().set(0,0,0))	; break; // stop							;
			case ALife::eHitTypeLightBurn  :										;//not stop
			case ALife::eHitTypeRadiation:											;//not stop
			case ALife::eHitTypeTelepatic:											;//not stop
			case ALife::eHitTypeChemicalBurn:										;break;//not stop
			case ALife::eHitTypeExplosion:											;//stop
			case ALife::eHitTypeFireWound:											;//stop
			case ALife::eHitTypeWound_2:											;break;//stop		//knife's alternative fire
//			case ALife::eHitTypePhysicStrike:	SetVelocity(Fvector().set(0,0,0))	;break;//stop
			default:																NODEFAULT	;
		}
	}
	//hit
	if(hit_type==ALife::eHitTypeExplosion||hit_type==ALife::eHitTypeWound)
																	ApplyImpulse(dir,P);
}

void CPHMovementControl::SetFrictionFactor(float f)
{
	m_character->FrictionFactor()=f;
}
float CPHMovementControl::GetFrictionFactor()
{
	return m_character->FrictionFactor();
}
void CPHMovementControl::MulFrictionFactor(float f)
{
	m_character->FrictionFactor()*=f;
}

IElevatorState	*CPHMovementControl::ElevatorState()
{
	if(!m_character || !m_character->b_exist)return NULL;
	return m_character->ElevatorState();
	//m_character->SetElevator()
}


struct STraceBorderQParams
{
	CPHMovementControl* m_movement;
	const Fvector &m_dir;	
	STraceBorderQParams(CPHMovementControl* movement,const Fvector &dir):
	m_dir(dir)
	{
		m_movement=movement		;
	}
	STraceBorderQParams& operator = (STraceBorderQParams& p) {VERIFY(FALSE);return p;}
};

BOOL CPHMovementControl::BorderTraceCallback(collide::rq_result& result, LPVOID params)
{
	STraceBorderQParams& p	= *(STraceBorderQParams*)params;
	u16 mtl_idx			=	GAMEMTL_NONE_IDX;
	CDB::TRI* T			=	NULL;
	if(result.O){
		return true;
	}else{
		//�������� ����������� � ������ ��� ��������
		T				= Level().ObjectSpace.GetStaticTris()+result.element;
		mtl_idx			= T->material;
	}
	VERIFY	(T);
	SGameMtl* mtl		= GMLib.GetMaterialByIdx(mtl_idx);
	if(mtl->Flags.test(SGameMtl::flInjurious))
	{
		Fvector tri_norm;
		GetNormal(T,tri_norm,Level().ObjectSpace.GetStaticVerts());
		if(p.m_dir.dotproduct(tri_norm)<0.f)p.m_movement->in_dead_area_count++;
		else p.m_movement->in_dead_area_count--;
	}
	return true;
}

void	CPHMovementControl::TraceBorder(const Fvector &prev_position)
{

	const Fvector	&from_pos			=prev_position						;
	const Fvector	&to_position		=vPosition							;
	Fvector dir;	dir					.sub(to_position,from_pos)			;
	float sq_mag	=					dir.square_magnitude()				;
	if(sq_mag==0.f) return													;
	float mag=_sqrt(sq_mag)													;
	dir.mul(1.f/mag)														;
	collide::ray_defs	RD		(from_pos,dir,mag,0,collide::rqtStatic)		;
	VERIFY							(!fis_zero(RD.dir.square_magnitude()))	;

	STraceBorderQParams			p(this,dir);
	storage.r_clear				();
	g_pGameLevel->ObjectSpace.RayQuery(storage,RD,BorderTraceCallback,&p,NULL,smart_cast<CObject*>(m_character->PhysicsRefObject()));
}

void	CPHMovementControl::				UpdateObjectBox(CPHCharacter *ach)
{
	if(!m_character||!m_character->b_exist) return;
	if(!ach||!ach->b_exist) return;
	Fvector cbox;
	PKinematics(pObject->Visual())->CalculateBones();
	pObject->BoundingBox().getradius(cbox);

	//const Fvector &pa	=cast_fv(dBodyGetPosition(ach->get_body()));
	//const Fvector &p	=cast_fv(dBodyGetPosition(m_character->get_body()));
	const Fvector &pa	=ach->BodyPosition();
	const Fvector &p	=m_character->BodyPosition();
	Fvector2 poses_dir;poses_dir.set(p.x-pa.x,p.z-pa.z);float plane_dist=poses_dir.magnitude(); 
	if(plane_dist>2.f) return;
	if(plane_dist>EPS_S)poses_dir.mul(1.f/plane_dist);
	Fvector2 plane_cam;plane_cam.set(Device.vCameraDirection.x,Device.vCameraDirection.z);plane_cam.normalize_safe();
	Fvector2 plane_i;plane_i.set(pObject->XFORM().i.x,pObject->XFORM().i.z);
	Fvector2 plane_k;plane_k.set(pObject->XFORM().k.x,pObject->XFORM().k.z);
	float R=_abs(poses_dir.dotproduct(plane_i)*cbox.x)+_abs(poses_dir.dotproduct(plane_k)*cbox.z);
	R*=poses_dir.dotproduct(plane_cam); //(poses_dir.x*plane_cam.x+poses_dir.y*plane_cam.z);
	Calculate(Fvector().set(0,0,0),Fvector().set(1,0,0),0,0,0,0);
	m_character->SetObjectRadius(R);
	ach->ChooseRestrictionType(rtStalker,1.f,m_character);
	m_character->UpdateRestrictionType(ach);
}

void CPHMovementControl::SetPathDir( const Fvector& v)
{
	_vPathDir = v;

	if(_abs(_vPathDir.x)>1000 || _abs(_vPathDir.y)>1000 || _abs(_vPathDir.z)>1000)
	{
		Log					("_vPathDir",	_vPathDir );
	
	}
	VERIFY2				( _abs(_vPathDir.x)<1000," incorrect SetPathDir ");

}
const IPhysicsElement*	CPHMovementControl::IElement( )const
{
	if(!CharacterExist()) 
		return 0;
	return m_character;
}




static const u32 move_steps_max_num = 20;
static const float move_velocity = 1.f;

static const float fmove_steps_max_num = move_steps_max_num;
void CPHMovementControl::VirtualMoveTo( const Fvector	&in_pos, Fvector &out_pos )
{
	VERIFY( CharacterExist() );
	VERIFY( _valid(in_pos) );

	class ph_character_state_save
	{
	public:
		ph_character_state_save ( CPHCharacter* character ): character_(character),  
						  saved_callback_( character->ObjectContactCallBack() ) 
		{
			character_->get_State( sv_state );
			///////////////////////////////////////
			character_->SetObjectContactCallback(virtual_move_collide_callback);
			character_->SetObjectContactCallbackData( static_cast<CPHObject*> (character) );
			character_->SwitchOFFInitContact();
			character_->SetApplyGravity( FALSE );
		}

		~ph_character_state_save()
		{
			character_->SetObjectContactCallback(saved_callback_);
			character_->SwitchInInitContact();
			character_->SetApplyGravity( TRUE );
			character_->SetObjectContactCallbackData( 0 );
			character_->set_State( sv_state );
		}

	private:
		SPHNetState				  sv_state;
		CPHCharacter*             character_;
		ObjectContactCallbackFun* saved_callback_; 
		
	} cleanup( m_character );


	
	const Fvector displacement = Fvector().sub( in_pos, vPosition );
	const float dist = displacement.magnitude();
	if( fis_zero( dist ) )
	{
		out_pos.set(in_pos);
		return;
	}
	
	float		move_time		= dist/move_velocity;
	float		n				= move_time/fixed_step;
	float		fsteps_num		= ceil( n );
	u32			steps_num		= iCeil( n );
	clamp( fsteps_num, 0.f, fmove_steps_max_num );
	clamp<u32>( steps_num, u32(0), move_steps_max_num );

	
					move_time		= fixed_step * fsteps_num;
	const float		calc_velocity	= dist/move_time;
	const float		force			= calc_velocity * m_character->Mass() / fixed_step;
	const Fvector	vforce			= Fvector().mul(displacement, force/dist );


	m_character->Enable();

	for( u32 i = 0; i< steps_num; ++i )
	{
		m_character->SetVelocity( Fvector().set(0,0,0) );
		m_character->setForce( vforce );
		m_character->step( fixed_step );
	}
	
	m_character->GetPosition( out_pos );
	VERIFY( _valid(out_pos) );
}




//static void	non_interactive_collide_callback( bool& do_collide, bool bo1, dContact& c, SGameMtl* material_1, SGameMtl* material_2 )
//{
//	if( !do_collide )
//		return;
//	
//	SGameMtl* oposite_matrial	= bo1 ? material_1 : material_2 ;
//	if(oposite_matrial->Flags.test(SGameMtl::flPassable))
//		return;
//
//	dxGeomUserData	*my_data			=	PHRetrieveGeomUserData(	bo1 ? c.geom.g1 : c.geom.g2 );
//	//dxGeomUserData	*oposite_data		=	PHRetrieveGeomUserData( bo1 ? c.geom.g2 : c.geom.g1 ) ;
//	VERIFY( my_data );
//	
//	dBodyID b_oposite = bo1 ? dGeomGetBody(c.geom.g2) : dGeomGetBody(c.geom.g1);
//	//dBodyID b_mine = bo1 ? dGeomGetBody(c.geom.g2) : dGeomGetBody(c.geom.g1);
//	if(!b_oposite)
//	{
//		do_collide = false;
//		return;
//	}
//	if(bo1)
//		dGeomSetBody(c.geom.g1,0);
//	else
//		dGeomSetBody(c.geom.g2,0);
//
//	//c.surface.mu = 0;
//	//c.surface.soft_cfm =0.01f;
//	/*
//	dJointID contact_joint	=dJointCreateContactSpecial(0, ContactGroup, &c);// dJointCreateContact(0, ContactGroup, &c);//
//	CPHObject* obj = (CPHObject*)my_data->ph_object;
//	VERIFY( obj );
//	VERIFY( obj->Island().DActiveIsland() != &(obj->Island()) );
//	VERIFY( !obj->Island().IsActive() );
//	obj->Island().DActiveIsland()->ConnectJoint(contact_joint);
//
//	obj->EnableObject(0);
//	if(bo1)
//		dJointAttach			(contact_joint, 0, b );
//	else
//		dJointAttach			(contact_joint, b , 0);
//		
//	*/
//}

void	CPHMovementControl::SetNonInteractive(bool v)
{
	VERIFY(m_character);
	if( !m_character->b_exist )
		return;
	if(bNonInteractiveMode == v) 
		return;
	if( v )
	{
		m_character->SetNonInteractive( v );
		//m_character->SetObjectContactCallback( non_interactive_collide_callback );
		m_character->Disable();
	}
	else
	{
		//m_character->SetObjectContactCallback( 0 );
		m_character->SetNonInteractive( v );
	}
	bNonInteractiveMode =v;

}

//dBodyID		CPHMovementControl::	GetBody						( )		
//{
//	if(m_character) return m_character->get_body(); else return NULL;
//}

void		CPHMovementControl::		GetCharacterVelocity		(Fvector& velocity )	
{
	if(m_character)
		m_character->GetVelocity(velocity); 
	else 
		velocity.set(0.f,0.f,0.f);
}


	void	CPHMovementControl::SetJumpUpVelocity		(float velocity)													
	{
		m_character->SetJupmUpVelocity(velocity);
	}
	void		CPHMovementControl::EnableCharacter			()																	
	{
		if( m_character && m_character->b_exist )
			m_character->Enable();
	}

			
	void		CPHMovementControl::		SetOjectContactCallback (ObjectContactCallbackFun* callback)
	{
		if(m_character)
			m_character->SetObjectContactCallback(callback);
	}
	void		CPHMovementControl::		SetFootCallBack			(ObjectContactCallbackFun* callback)
	{
		VERIFY(m_character);
		m_character->SetWheelContactCallback(callback);
	}

	ObjectContactCallbackFun* CPHMovementControl::ObjectContactCallback()			
	{
		if(m_character)
			return m_character->ObjectContactCallBack();
		else return NULL; 
	}
	u16	CPHMovementControl::ContactBone				()				
	{
		return m_character->ContactBone();
	}
	const ICollisionDamageInfo	*CPHMovementControl::CollisionDamageInfo ()const	
	{
		VERIFY(m_character);
		return m_character->CollisionDamageInfo ();
	}
	ICollisionDamageInfo	*CPHMovementControl::CollisionDamageInfo ()
	{
		VERIFY(m_character);
		return m_character->CollisionDamageInfo ();
	}
	void	CPHMovementControl::GetDesiredPos(Fvector& dpos)
	{	
		m_character->GetDesiredPosition(dpos);
	}
	bool	CPHMovementControl::CharacterExist() const		
	{
		return ( m_character && m_character->b_exist );
	}
	void CPHMovementControl::update_last_material	()
	{ 
		VERIFY(m_character);
		m_character->update_last_material	();  
	}
	u16	CPHMovementControl::injurious_material_idx	()
	{ 
		VERIFY(m_character);
		return m_character->InjuriousMaterialIDX(); 
	}

	void	CPHMovementControl::SetApplyGravity			(BOOL flag)	
	{
		bIsAffectedByGravity=flag;
		if( m_character && m_character->b_exist )
				m_character->SetApplyGravity( flag ); 
	}
	void	CPHMovementControl::GetDeathPosition(Fvector& pos)
	{ 
		VERIFY( m_character );
		m_character->DeathPosition(pos);
	}

	bool	CPHMovementControl::	IsCharacterEnabled()	
	{
		return m_character->IsEnabled()||bExernalImpulse||bNonInteractiveMode;
	}
	void	CPHMovementControl::DisableCharacter()
	{
		VERIFY( m_character );
		m_character->Disable();
	}

	void	CPHMovementControl::GetCharacterPosition(Fvector &P)
	{ 
		VERIFY(m_character);
		m_character->GetPosition(P);
	}
	void		CPHMovementControl::InterpolatePosition(Fvector	&P)
	{
		VERIFY(m_character&&m_character->b_exist);
		m_character->IPosition(P);
	}
	void		CPHMovementControl::SetMass(float M)	
	{ 
		fMass = M;
		if(m_character)
			m_character->SetMas(fMass);
	}
	float		CPHMovementControl::FootRadius					( )		
	{
		if(m_character)
			return m_character->FootRadius(); 
		else 
			return 0.f;
	}
	void		CPHMovementControl::CollisionEnable 			(BOOL enable)
	{
		if( !m_character || !m_character->b_exist ) 
			return;
		if(enable)
			m_character->collision_enable();
		else 
			m_character->collision_disable();
		
	}

	void	CPHMovementControl::SetCharacterVelocity		(const Fvector& v)		
	{
		if(m_character)
			m_character->SetVelocity(v);
	}

	void	CPHMovementControl::SetPhysicsRefObject	(CPhysicsShellHolder* ref_object)
	{ 
		VERIFY(m_character); 
		m_character->SetPhysicsRefObject(ref_object);
	}

	void		CPHMovementControl::GetSmoothedVelocity			(Fvector& v)
	{
		if(m_character)
			m_character->GetSmothedVelocity(v);
		else 
			v.set(0,0,0);
	}

	void		CPHMovementControl::SetPLastMaterialIDX	(u16* p)
	{
		VERIFY(m_character);
		m_character->SetPLastMaterialIDX(p);
	}

#ifdef DEBUG
	void		CPHMovementControl::		dbg_Draw(){
#if 0
		if(m_character)
			m_character->OnRender();
#endif
	};
#endif

bool		CPHMovementControl::JumpState( )
{ 
	return ( m_character&&m_character->b_exist	&& 
			 m_character->IsEnabled( )			&& 
			 m_character->JumpState( ) 
			);
}
///
bool		CPHMovementControl::PhysicsOnlyMode( )
{ 
	return	m_character				&& 
			m_character->b_exist	&&
			m_character->IsEnabled()&&
			( m_character->JumpState( ) || m_character->ForcedPhysicsControl( ) )
			; 
}

void		CPHMovementControl::SetRestrictionType( ERestrictionType rt)
{ 
	if( m_character ) 
		m_character->SetRestrictionType( rt ); 
}
void	CPHMovementControl::SetActorMovable( bool v )
{ 
	if( m_character ) 
		m_character->SetActorMovable( v ); 
}
void	CPHMovementControl::SetForcedPhysicsControl( bool v )
{ 
	if( m_character ) 
		m_character->SetForcedPhysicsControl( v ); 
}
bool	CPHMovementControl::ForcedPhysicsControl( )
{
	return m_character && m_character->ForcedPhysicsControl( );
}
IPHCapture*		CPHMovementControl::		PHCapture			( )
{
	return m_capture; 
}

IPhysicsShellHolder* CPHMovementControl::PhysicsRefObject			()
{
	VERIFY( m_character );
	return m_character->PhysicsRefObject();
}

void	CPHMovementControl::			actor_calculate			(Fvector& vAccel,const Fvector& camDir, float ang_speed, float jump, float dt, bool bLight)
{
	Calculate(  vAccel,camDir, ang_speed, jump, dt, bLight );
}

void			CPHMovementControl::BlockDamageSet		( u64 steps_num )
{
	block_damage_step_end = physics_world()->StepsNum() + steps_num;
	UpdateCollisionDamage();//reset all saved values
}

void	CPHMovementControl::NetRelcase	( CObject* O )
{
	
	CPhysicsShellHolder *sh = smart_cast<CPhysicsShellHolder *>(O);
	if(!sh)
		return;
	IPHCapture* c=PHCapture();
	if( c )
		c->RemoveConnection( sh );

	if( m_character )
		m_character->NetRelcase( sh );
}