#include "stdafx.h"
#include "xrServer.h"
#include "xrserver_objects.h"
#include "game_base.h"
#include "game_cl_base.h"

void xrServer::Process_event_destroy	(NET_Packet& P, ClientID sender, u32 time, u16 ID, NET_Packet* pEPack)
{
	u32								MODE = net_flags(TRUE,TRUE);
	// Parse message
	u16								id_dest	= ID;

	CSE_Abstract*					e_dest = game->get_entity_from_eid	(id_dest);	// ��� ������ ���� ���������
	if (!e_dest) 
	{
#ifndef MASTER_GOLD
		Msg							("!SV:ge_destroy: [%d] not found on server",id_dest);
#endif // #ifndef MASTER_GOLD
		return;
	};

	R_ASSERT						(e_dest);
	xrClientData					*c_dest = e_dest->owner;				// ������, ��� ����
	R_ASSERT						(c_dest);
	xrClientData					*c_from = ID_to_client(sender);	// ������, ��� �������
	R_ASSERT						(c_dest == c_from);							// assure client ownership of event
	u16								parent_id = e_dest->ID_Parent;

#ifdef MP_LOGGING
	Msg("--- SV: Process destroy: parent [%d] item [%d][%s]", 
		parent_id, id_dest, e_dest->name());
#endif //#ifdef MP_LOGGING

	//---------------------------------------------
	NET_Packet	P2, *pEventPack = pEPack;
	P2.w_begin	(M_EVENT_PACK);
	//---------------------------------------------
	// check if we have children 
	if (!e_dest->children.empty()) {
		if (!pEventPack) pEventPack = &P2;

		while (!e_dest->children.empty())
			Process_event_destroy		(P,sender,time,*e_dest->children.begin(), pEventPack);
	};

	if (0xffff == parent_id && NULL == pEventPack) 
	{
		SendBroadcast				(BroadcastCID,P,MODE);
	}
	else 
	{
		NET_Packet	tmpP;
		if (0xffff != parent_id && Process_event_reject(P,sender,time,parent_id,ID,false)) 
		{
			game->u_EventGen(tmpP, GE_OWNERSHIP_REJECT, parent_id);
			tmpP.w_u16(id_dest);
			tmpP.w_u8(1);
		
			if (!pEventPack) pEventPack = &P2;
			
			pEventPack->w_u8(u8(tmpP.B.count));
			pEventPack->w(&tmpP.B.data, tmpP.B.count);
		};
		
 		game->u_EventGen(tmpP, GE_DESTROY, id_dest);
		
		pEventPack->w_u8(u8(tmpP.B.count));
		pEventPack->w(&tmpP.B.data, tmpP.B.count);
	};

	if (NULL == pEPack && NULL != pEventPack)
	{
		SendBroadcast				(BroadcastCID, *pEventPack, MODE);
	}

	if (game)
		game->OnDestroyObject		(e_dest->ID);

	entity_Destroy					(e_dest);
}
