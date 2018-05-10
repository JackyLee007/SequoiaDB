/*******************************************************************************

   Copyright (C) 2011-2014 SequoiaDB Ltd.

   This program is free software: you can redistribute it and/or modify
   it under the term of the GNU Affero General Public License, version 3,
   as published by the Free Software Foundation.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warrenty of
   MARCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU Affero General Public License for more details.

   You should have received a copy of the GNU Affero General Public License
   along with this program. If not, see <http://www.gnu.org/license/>.

   Source File Name = rtnCoordCommon.cpp

   Descriptive Name = Runtime Coord Common

   When/how to use: this program may be used on binary and text-formatted
   versions of runtime component. This file contains code logic for
   common functions for coordinator node.

   Dependencies: N/A

   Restrictions: N/A

   Change Activity:
   defect Date        Who Description
   ====== =========== === ==============================================

   Last Changed =

*******************************************************************************/

#include "rtnCoord.hpp"
#include "rtnCoordCommon.hpp"
#include "msg.hpp"
#include "pmd.hpp"
#include "pmdCB.hpp"
#include "rtnCoordQuery.hpp"
#include "msgMessage.hpp"
#include "coordCB.hpp"
#include "rtnContext.hpp"
#include "pdTrace.hpp"
#include "rtnTrace.hpp"
#include "coordSession.hpp"
#include "rtnCoordCommands.hpp"
#include "rtn.hpp"

using namespace bson;

namespace engine
{
   /*
      Local define
   */
   #define RTN_COORD_RSP_WAIT_TIME        1000     //1s
   #define RTN_COORD_RSP_WAIT_TIME_QUICK  10       //10ms

   #define RTN_COORD_MAX_RETRYTIMES       2

   /*
      Local function define
   */

   // PD_TRACE_DECLARE_FUNCTION ( SDB__RTNCOSENDREQUESTTOONE, "_rtnCoordSendRequestToOne" )
   static INT32 _rtnCoordSendRequestToOne( MsgHeader *pBuffer,
                                           CoordGroupInfoPtr &groupInfo,
                                           REQUESTID_MAP &sendNodes, // out
                                           netMultiRouteAgent *pRouteAgent,
                                           MSG_ROUTE_SERVICE_TYPE type,
                                           pmdEDUCB *cb,
                                           const netIOVec *pIOVec,
                                           BOOLEAN isResend )
   {
      INT32 rc                = SDB_OK ;
      CoordSession *pSession  = cb->getCoordSession() ;
      MsgRouteID routeID ;
      clsGroupItem *groupItem = NULL ;
      UINT32 nodeNum          = 0 ;
      UINT32 beginPos         = 0 ;
      UINT32 selTimes         = 0 ;

      BOOLEAN hasRetry        = FALSE ;
      UINT64 reqID            = 0 ;

      RTN_COORD_POS_LIST selectedPositions ;

      PD_TRACE_ENTRY ( SDB__RTNCOSENDREQUESTTOONE ) ;

      if ( pSession )
      {
         if ( pSession->isMasterPreferred() )
         {
            if ( MSG_BS_QUERY_REQ == pBuffer->opCode )
            {
               MsgOpQuery *pQuery = ( MsgOpQuery* )pBuffer ;
               if ( FALSE == isResend )
               {
                  pQuery->flags |= FLG_QUERY_PRIMARY ;
               }
               else
               {
                  pQuery->flags &= ~FLG_QUERY_PRIMARY ;
               }
            }
            else if ( pBuffer->opCode > ( SINT32 )MSG_LOB_BEGIN &&
                      pBuffer->opCode < ( SINT32 )MSG_LOB_END )
            {
               MsgOpLob *pLobMsg = ( MsgOpLob* )pBuffer ;
               if ( FALSE == isResend )
               {
                  pLobMsg->flags |= FLG_LOBREAD_PRIMARY ;
               }
               else
               {
                  pLobMsg->flags &= ~FLG_LOBREAD_PRIMARY ;
               }
            }
         }
      }

      /*******************************
      ********************************/
      if ( NULL != pSession )
      {
         routeID = pSession->getLastNode( groupInfo->groupID() ) ;
         if ( routeID.value != 0 &&
              groupInfo->nodePos( routeID.columns.nodeID ) >= 0 )
         {
            if ( pIOVec && pIOVec->size() > 0 )
            {
               rc = pRouteAgent->syncSend( routeID, pBuffer, *pIOVec,
                                           reqID, cb ) ;
            }
            else
            {
               rc = pRouteAgent->syncSend( routeID, (void *)pBuffer,
                                           reqID, cb, NULL, 0 ) ;
            }
            if ( SDB_OK == rc )
            {
               sendNodes[reqID] = routeID ;
               goto done ;
            }
            else
            {
               rtnCoordUpdateNodeStatByRC( cb, routeID, groupInfo, rc ) ;

               PD_LOG( PDWARNING, "Send msg[opCode: %d, TID: %u] to group[%u] 's "
                       "last node[%s] failed, rc: %d", pBuffer->opCode,
                       pBuffer->TID, groupInfo->groupID(),
                       routeID2String( routeID ).c_str(), rc ) ;
            }
         }
      }

   retry:
      /*******************************
      ********************************/
      groupItem = groupInfo.get() ;
      nodeNum = groupInfo->nodeCount() ;
      selectedPositions.clear() ;

      if ( nodeNum <= 0 )
      {
         if ( !hasRetry && CATALOG_GROUPID != groupInfo->groupID() )
         {
            hasRetry = TRUE ;
            rc = rtnCoordGetGroupInfo( cb, groupInfo->groupID(),
                                       TRUE, groupInfo ) ;
            PD_RC_CHECK( rc, PDERROR, "Get group info[%u] failed, rc: %d",
                         groupInfo->groupID(), rc ) ;
            goto retry ;
         }
         rc = SDB_CLS_EMPTY_GROUP ;
         PD_LOG ( PDERROR, "Couldn't send the request to empty group" ) ;
         goto error ;
      }

      routeID.value = MSG_INVALID_ROUTEID ;
      rtnCoordGetNodePos( groupItem, pSession->getInstanceOption(), ossRand(),
                          beginPos, selectedPositions ) ;

      selTimes = 0 ;
      while( selTimes < nodeNum )
      {
         INT32 status = NET_NODE_STAT_NORMAL ;
         rtnCoordGetNextNode( groupItem, selectedPositions,
                              pSession->getInstanceOption(), selTimes,
                              beginPos ) ;

         rc = groupItem->getNodeID( beginPos, routeID, type ) ;
         if ( rc )
         {
            break ;
         }
         rc = groupInfo->getNodeInfo( beginPos, status ) ;
         if ( rc )
         {
            break ;
         }
         if ( NET_NODE_STAT_NORMAL == status )
         {
            if ( pIOVec )
            {
               rc = pRouteAgent->syncSend( routeID, pBuffer, *pIOVec,
                                           reqID, cb ) ;
            }
            else
            {
               rc = pRouteAgent->syncSend( routeID, (void *)pBuffer,
                                           reqID, cb, NULL, 0 ) ;
            }
            if ( SDB_OK == rc )
            {
               sendNodes[ reqID ] = routeID ;

               if ( pSession )
               {
                  pSession->addLastNode( routeID ) ;
               }
               break ;
            }
            else
            {
               rtnCoordUpdateNodeStatByRC( cb, routeID, groupInfo, rc ) ;
            }
         }
         else
         {
            rc = SDB_CLS_NODE_BSFAULT ;
         }
      }

      if ( rc && !hasRetry )
      {
         hasRetry = TRUE ;
         if ( CATALOG_GROUPID != groupInfo->groupID() )
         {
            rc = rtnCoordGetGroupInfo( cb, groupInfo->groupID(),
                                       TRUE, groupInfo ) ;
            PD_RC_CHECK( rc, PDERROR, "Get group info[%u] failed, rc: %d",
                         groupInfo->groupID(), rc ) ;
         }
         else
         {
            groupInfo->clearNodesStat() ;
            rc = SDB_OK ;
         }
         goto retry ;
      }

   done:
      PD_TRACE_EXITRC ( SDB__RTNCOSENDREQUESTTOONE, rc ) ;
      return rc ;
   error:
      goto done ;
   }

   // PD_TRACE_DECLARE_FUNCTION ( SDB__RTNCOSENDREQUESTTOPRIMARY, "_rtnCoordSendRequestToPrimary" )
   static INT32 _rtnCoordSendRequestToPrimary( MsgHeader *pBuffer,
                                               CoordGroupInfoPtr &groupInfo,
                                               netMultiRouteAgent *pRouteAgent,
                                               MSG_ROUTE_SERVICE_TYPE type,
                                               pmdEDUCB *cb,
                                               REQUESTID_MAP &sendNodes, // out
                                               const netIOVec *pIOVec )
   {
      INT32 rc = SDB_OK ;
      PD_TRACE_ENTRY ( SDB__RTNCOSENDREQUESTTOPRIMARY ) ;

      MsgRouteID primaryRouteID ;
      UINT64 reqID = 0 ;
      BOOLEAN hasRetry = FALSE ;
      CoordSession *pSession  = cb->getCoordSession() ;

   retry:
      primaryRouteID = groupInfo->primary( type ) ;
      if ( primaryRouteID.value != 0 )
      {
         if ( pIOVec && pIOVec->size() > 0 )
         {
            rc = pRouteAgent->syncSend( primaryRouteID, pBuffer, *pIOVec,
                                        reqID, cb ) ;
         }
         else
         {
            rc = pRouteAgent->syncSend( primaryRouteID, (void *)pBuffer,
                                        reqID, cb, NULL, 0 ) ;
         }
         if ( SDB_OK == rc )
         {
            sendNodes[ reqID ] = primaryRouteID ;
            if ( pSession )
            {
               pSession->addLastNode( primaryRouteID ) ;
            }
            goto done ;
         }
         else
         {
            rtnCoordUpdateNodeStatByRC( cb, primaryRouteID, groupInfo, rc ) ;

            if ( !hasRetry )
            {
               goto update ;
            }

            PD_LOG( PDWARNING, "Send msg[opCode: %d, TID: %u] to group[%u] 's "
                    "primary node[%s] failed, rc: %d", pBuffer->opCode,
                    pBuffer->TID, groupInfo->groupID(),
                    routeID2String( primaryRouteID ).c_str(), rc ) ;
         }
      }

      rc = SDB_RTN_NO_PRIMARY_FOUND ;
      if ( !hasRetry )
      {
         goto update ;
      }
      rc = _rtnCoordSendRequestToOne( pBuffer, groupInfo, sendNodes,
                                      pRouteAgent, type, cb,
                                      pIOVec, TRUE ) ;
      goto done ;

   update:
      if ( rc && !hasRetry )
      {
         hasRetry = TRUE ;
         rc = rtnCoordGetGroupInfo( cb, groupInfo->groupID(),
                                    TRUE, groupInfo ) ;
         PD_RC_CHECK( rc, PDERROR, "Get group info[%u] failed, rc: %d",
                      groupInfo->groupID(), rc ) ;
         goto retry ;
      }
   done:
      PD_TRACE_EXITRC ( SDB__RTNCOSENDREQUESTTOPRIMARY, rc ) ;
      return rc ;
   error:
      goto done ;
   }

   // PD_TRACE_DECLARE_FUNCTION ( SDB__RTNCOSENDREQUESTTONODEGROUP, "_rtnCoordSendRequestToNodeGroup" )
   static INT32 _rtnCoordSendRequestToNodeGroup( MsgHeader *pBuffer,
                                                 CoordGroupInfoPtr &groupInfo,
                                                 BOOLEAN isSendPrimary,
                                                 netMultiRouteAgent *pRouteAgent,
                                                 MSG_ROUTE_SERVICE_TYPE type,
                                                 pmdEDUCB *cb,
                                                 REQUESTID_MAP &sendNodes, // out
                                                 const netIOVec *pIOVec,
                                                 BOOLEAN isResend )
   {
      INT32 rc = SDB_OK;
      PD_TRACE_ENTRY ( SDB__RTNCOSENDREQUESTTONODEGROUP ) ;

      MsgRouteID routeID ;
      UINT64 reqID = 0 ;
      UINT32 groupID = groupInfo->groupID() ;

      cb->getTransNodeRouteID( groupID, routeID ) ;
      if ( routeID.value != 0 )
      {
         if ( pIOVec && pIOVec->size() > 0 )
         {
            rc = pRouteAgent->syncSend( routeID, pBuffer, *pIOVec,
                                        reqID, cb ) ;
         }
         else
         {
            rc = pRouteAgent->syncSend( routeID, (void *)pBuffer,
                                        reqID, cb, NULL, 0 ) ;
         }
         if ( SDB_OK == rc )
         {
            sendNodes[ reqID ] = routeID ;
         }
         else
         {
            PD_LOG( PDERROR, "Send msg[opCode: %d, TID: %u] to group[%u] 's "
                    "transaction node[%s] failed, rc: %d", pBuffer->opCode,
                    pBuffer->TID, groupID, routeID2String( routeID ).c_str(),
                    rc ) ;
            rtnCoordUpdateNodeStatByRC( cb, routeID, groupInfo, rc ) ;
            goto error ;
         }
      }
      else
      {
         if ( isSendPrimary )
         {
            rc = _rtnCoordSendRequestToPrimary( pBuffer, groupInfo,
                                                pRouteAgent, type, cb,
                                                sendNodes, pIOVec ) ;
         }
         else
         {
            rc = _rtnCoordSendRequestToOne( pBuffer, groupInfo, sendNodes,
                                            pRouteAgent, type, cb,
                                            pIOVec, isResend ) ;
         }
         PD_RC_CHECK( rc, PDERROR, "Send msg[opCode: %d, TID: %u] to "
                      "group[%u]'s %s node failed, rc: %d",
                      pBuffer->opCode, pBuffer->TID, groupID,
                      isSendPrimary ? "primary" : "one", rc ) ; 
      }

   done:
      PD_TRACE_EXITRC ( SDB__RTNCOSENDREQUESTTONODEGROUP, rc ) ;
      return rc ;
   error:
      goto done ;
   }

   #define RTN_COORD_PARSE_MASK_ET_DFT       0x00000001
   #define RTN_COORD_PARSE_MASK_IN_DFT       0x00000002
   #define RTN_COORD_PARSE_MASK_ET_OPR       0x00000004
   #define RTN_COORD_PARSE_MASK_IN_OPR       0x00000008

   #define RTN_COORD_PARSE_MASK_ET           ( RTN_COORD_PARSE_MASK_ET_DFT|\
                                               RTN_COORD_PARSE_MASK_ET_OPR )
   #define RTN_COORD_PARSE_MASK_ALL          0xFFFFFFFF

   static INT32 _rtnCoordParseBoolean( BSONElement &e, BOOLEAN &value,
                                       UINT32 mask )
   {
      INT32 rc = SDB_INVALIDARG ;
      if ( e.isBoolean() && ( mask & RTN_COORD_PARSE_MASK_ET_DFT ) )
      {
         value = e.boolean() ? TRUE : FALSE ;
         rc = SDB_OK ;
      }
      else if ( e.isNumber() && ( mask & RTN_COORD_PARSE_MASK_ET_DFT ) )
      {
         value = 0 != e.numberInt() ? TRUE : FALSE ;
         rc = SDB_OK ;
      }
      else if ( Object == e.type() && ( mask & RTN_COORD_PARSE_MASK_ET_OPR ) )
      {
         BSONObj obj = e.embeddedObject() ;
         BSONElement tmpE = obj.firstElement() ;
         if ( 1 == obj.nFields() &&
              0 == ossStrcmp( "$et", tmpE.fieldName() ) )
         {
            rc = _rtnCoordParseBoolean( tmpE, value,
                                        RTN_COORD_PARSE_MASK_ET_DFT ) ;
         }
      }
      return rc ;
   }

   static INT32 _rtnCoordParseInt( BSONElement &e, INT32 &value, UINT32 mask )
   {
      INT32 rc = SDB_INVALIDARG ;
      if ( e.isNumber() && ( mask & RTN_COORD_PARSE_MASK_ET_DFT ) )
      {
         value = e.numberInt() ;
         rc = SDB_OK ;
      }
      else if ( Object == e.type() && ( mask & RTN_COORD_PARSE_MASK_ET_OPR ) )
      {
         BSONObj obj = e.embeddedObject() ;
         BSONElement tmpE = obj.firstElement() ;
         if ( 1 == obj.nFields() &&
              0 == ossStrcmp( "$et", tmpE.fieldName() ) )
         {
            rc = _rtnCoordParseInt( tmpE, value,
                                    RTN_COORD_PARSE_MASK_ET_DFT ) ;
         }
      }
      return rc ;
   }

   static INT32 _rtnCoordParseInt( BSONElement &e, vector<INT32> &vecValue,
                                   UINT32 mask )
   {
      INT32 rc = SDB_INVALIDARG ;
      INT32 value = 0 ;

      if ( ( !e.isABSONObj() ||
             0 == ossStrcmp( e.embeddedObject().firstElement().fieldName(),
                             "$et") ) &&
           ( mask & RTN_COORD_PARSE_MASK_ET ) )
      {
         rc = _rtnCoordParseInt( e, value, mask ) ;
         if ( SDB_OK == rc )
         {
            vecValue.push_back( value ) ;
         }
      }
      else if ( Array == e.type() && ( mask & RTN_COORD_PARSE_MASK_IN_DFT ) )
      {
         BSONObjIterator it( e.embeddedObject() ) ;
         while ( it.more() )
         {
            BSONElement tmpE = it.next() ;
            rc = _rtnCoordParseInt( tmpE, value,
                                    RTN_COORD_PARSE_MASK_ET_DFT ) ;
            if ( rc )
            {
               break ;
            }
            vecValue.push_back( value ) ;
         }
      }
      else if ( Object == e.type() &&
                0 == ossStrcmp( e.embeddedObject().firstElement().fieldName(),
                                "$in") &&
                ( mask & RTN_COORD_PARSE_MASK_IN_OPR ) )
      {
         BSONObj obj = e.embeddedObject() ;
         BSONElement tmpE = obj.firstElement() ;
         if ( 1 == obj.nFields() )
         {
            rc = _rtnCoordParseInt( tmpE, vecValue,
                                    RTN_COORD_PARSE_MASK_IN_DFT ) ;
         }
      }
      return rc ;
   }

   static INT32 _rtnCoordParseString( BSONElement &e, const CHAR *&value,
                                      UINT32 mask )
   {
      INT32 rc = SDB_INVALIDARG ;
      if ( String == e.type() && ( mask & RTN_COORD_PARSE_MASK_ET_DFT ) )
      {
         value = e.valuestr() ;
         rc = SDB_OK ;
      }
      else if ( Object == e.type() && ( mask & RTN_COORD_PARSE_MASK_ET_OPR ) )
      {
         BSONObj obj = e.embeddedObject() ;
         BSONElement tmpE = obj.firstElement() ;
         if ( 1 == obj.nFields() &&
              0 == ossStrcmp( "$et", tmpE.fieldName() ) )
         {
            rc = _rtnCoordParseString( tmpE, value,
                                       RTN_COORD_PARSE_MASK_ET_DFT ) ;
         }
      }
      return rc ;
   }

   static INT32 _rtnCoordParseString( BSONElement &e,
                                      vector<const CHAR*> &vecValue,
                                      UINT32 mask )
   {
      INT32 rc = SDB_INVALIDARG ;
      const CHAR *value = NULL ;

      if ( ( !e.isABSONObj() ||
             0 == ossStrcmp( e.embeddedObject().firstElement().fieldName(),
                             "$et") ) &&
           ( mask & RTN_COORD_PARSE_MASK_ET ) )
      {
         rc = _rtnCoordParseString( e, value, mask ) ;
         if ( SDB_OK == rc )
         {
            vecValue.push_back( value ) ;
         }
      }
      else if ( Array == e.type() && ( mask & RTN_COORD_PARSE_MASK_IN_DFT ) )
      {
         BSONObjIterator it( e.embeddedObject() ) ;
         while ( it.more() )
         {
            BSONElement tmpE = it.next() ;
            rc = _rtnCoordParseString( tmpE, value,
                                       RTN_COORD_PARSE_MASK_ET_DFT ) ;
            if ( rc )
            {
               break ;
            }
            vecValue.push_back( value ) ;
         }
      }
      else if ( Object == e.type() &&
                0 == ossStrcmp( e.embeddedObject().firstElement().fieldName(),
                                "$in") &&
                ( mask & RTN_COORD_PARSE_MASK_IN_OPR ) )
      {
         BSONObj obj = e.embeddedObject() ;
         BSONElement tmpE = obj.firstElement() ;
         if ( 1 == obj.nFields() )
         {
            rc = _rtnCoordParseString( tmpE, vecValue,
                                       RTN_COORD_PARSE_MASK_IN_DFT ) ;
         }
      }
      return rc ;
   }

   /*
      End local function define
   */

   // PD_TRACE_DECLARE_FUNCTION ( SDB_RTNCOCATAQUERY, "rtnCoordCataQuery" )
   INT32 rtnCoordCataQuery ( const CHAR *pCollectionName,
                             const BSONObj &selector,
                             const BSONObj &matcher,
                             const BSONObj &orderBy,
                             const BSONObj &hint,
                             INT32 flag,
                             pmdEDUCB *cb,
                             SINT64 numToSkip,
                             SINT64 numToReturn,
                             SINT64 &contextID,
                             rtnContextBuf *buf )
   {
      INT32 rc = SDB_OK;
      PD_TRACE_ENTRY ( SDB_RTNCOCATAQUERY ) ;
      contextID = -1 ;
      rtnCoordCommand commandOpr ;
      rtnQueryOptions queryOpt( matcher.objdata(),
                                selector.objdata(),
                                orderBy.objdata(),
                                hint.objdata(),
                                pCollectionName,
                                numToSkip,
                                numToReturn,
                                flag,
                                FALSE ) ;
      rc = commandOpr.queryOnCatalog( queryOpt, cb, contextID, buf ) ;
      PD_RC_CHECK( rc, PDERROR, "Query[%s] on catalog failed, rc: %d",
                   queryOpt.toString().c_str(), rc ) ;

   done:
      PD_TRACE_EXITRC ( SDB_RTNCOCATAQUERY, rc ) ;
      return rc ;
   error:
      goto done;
   }

   // PD_TRACE_DECLARE_FUNCTION ( SDB_RTNCONODEQUERY, "rtnCoordNodeQuery" )
   INT32 rtnCoordNodeQuery ( const CHAR *pCollectionName,
                             const BSONObj &condition,
                             const BSONObj &selector,
                             const BSONObj &orderBy,
                             const BSONObj &hint,
                             INT64 numToSkip, INT64 numToReturn,
                             CoordGroupList &groupLst,
                             pmdEDUCB *cb,
                             rtnContext **ppContext,
                             const CHAR *realCLName,
                             INT32 flag,
                             rtnContextBuf *pBuf )
   {
      INT32 rc                        = SDB_OK ;
      PD_TRACE_ENTRY ( SDB_RTNCONODEQUERY ) ;
      CHAR *pBuffer                   = NULL ;
      INT32 bufferSize                = 0 ;

      pmdKRCB *pKrcb                  = pmdGetKRCB() ;
      SDB_RTNCB *pRtnCB               = pKrcb->getRTNCB() ;
      CoordCB *pCoordcb               = pKrcb->getCoordCB() ;
      netMultiRouteAgent *pRouteAgent = pCoordcb->getRouteAgent();

      SDB_ASSERT ( ppContext, "ppContext can't be NULL" ) ;
      SDB_ASSERT ( cb, "cb can't be NULL" ) ;

      rtnCoordQuery newQuery ;
      rtnQueryConf queryConf ;
      rtnSendOptions sendOpt ;

      BOOLEAN needReset = FALSE ;
      BOOLEAN onlyOneNode = groupLst.size() == 1 ? TRUE : FALSE ;
      INT64 tmpSkip = numToSkip ;
      INT64 tmpReturn = numToReturn ;

      if ( !onlyOneNode )
      {
         if ( numToReturn > 0 && numToSkip > 0 )
         {
            tmpReturn = numToReturn + numToSkip ;
         }
         tmpSkip = 0 ;

         rtnNeedResetSelector( selector, orderBy, needReset ) ;
      }
      else
      {
         numToSkip = 0 ;
      }

      const CHAR *realCLFullName = realCLName ? realCLName : pCollectionName ;
      rtnContextCoord *pTmpContext = NULL ;
      INT64 contextID = -1 ;

      if ( groupLst.size() == 0 )
      {
         PD_LOG( PDERROR, "Send group list is empty" ) ;
         rc = SDB_SYS ;
         goto error ;
      }

      rc = pRtnCB->contextNew ( RTN_CONTEXT_COORD, (rtnContext**)&pTmpContext,
                                contextID, cb ) ;
      if ( rc )
      {
         PD_LOG ( PDERROR, "Failed to allocate new context, rc = %d",
                  rc ) ;
         goto error ;
      }
      rc = pTmpContext->open( orderBy,
                              needReset ? selector : BSONObj(),
                              numToReturn, numToSkip ) ;
      if ( rc )
      {
         PD_LOG( PDERROR, "Open context failed, rc: %d", rc ) ;
         goto error ;
      }

      rc = msgBuildQueryMsg ( &pBuffer, &bufferSize, pCollectionName,
                              flag, 0, tmpSkip, tmpReturn,
                              condition.isEmpty()?NULL:&condition,
                              selector.isEmpty()?NULL:&selector,
                              orderBy.isEmpty()?NULL:&orderBy,
                              hint.isEmpty()?NULL:&hint ) ;
      if ( rc )
      {
         PD_LOG ( PDERROR, "Failed to build query message, rc = %d",
                  rc ) ;
         goto error ;
      }

      queryConf._realCLName = realCLFullName ;
      sendOpt._groupLst = groupLst ;
      sendOpt._useSpecialGrp = TRUE ;

      rc = newQuery.queryOrDoOnCL( (MsgHeader*)pBuffer, pRouteAgent, cb,
                                   &pTmpContext, sendOpt, &queryConf,
                                   pBuf ) ;
      PD_RC_CHECK( rc, PDERROR, "Query collection[%s] from data node "
                   "failed, rc = %d", realCLFullName, rc ) ;

   done :
      if ( pBuffer )
      {
         SDB_OSS_FREE ( pBuffer ) ;
      }
      if ( pTmpContext )
      {
         *ppContext = pTmpContext ;
      }
      PD_TRACE_EXITRC ( SDB_RTNCONODEQUERY, rc ) ;
      return rc ;
   error :
      if ( contextID >= 0 )
      {
         pRtnCB->contextDelete ( contextID, cb ) ;
         pTmpContext = NULL ;
      }
      goto done ;
   }

   static INT32 _rtnCoordProcessExpiredReply ( pmdEDUCB * cb,
                                               MsgHeader * pReply,
                                               rtnContextCoord ** expiredContext )
   {
      INT32 rc = SDB_OK ;


      SDB_RTNCB * rtnCB = pmdGetKRCB()->getRTNCB() ;
      INT64 contextID = -1 ;
      MsgOpReply * pOpReply = NULL ;

      if ( NULL == pReply ||
           !IS_REPLY_TYPE( pReply->opCode ) ||
           NULL == expiredContext )
      {
         goto done ;
      }

      pOpReply = (MsgOpReply *)pReply ;
      if ( -1 == pOpReply->contextID )
      {
         goto done ;
      }

      PD_LOG( PDWARNING, "Received expired context [%lld] from node [%s]",
              pOpReply->contextID, routeID2String( pReply->routeID ).c_str() ) ;

      if ( NULL == (*expiredContext) )
      {
         BSONObj dummy ;

         rc = rtnCB->contextNew ( RTN_CONTEXT_COORD, (rtnContext**)expiredContext,
                                  contextID, cb ) ;
         PD_RC_CHECK( rc, PDWARNING, "Failed to create dummy coord context, "
                      "rc: %d", rc ) ;

         rc = (*expiredContext)->open( dummy, dummy, -1, 0, FALSE ) ;
         PD_RC_CHECK( rc, PDWARNING, "Failed to open dummy coord context, "
                      "rc: %d", rc ) ;
      }

      rc = (*expiredContext)->addSubContext( pReply->routeID,
                                             pOpReply->contextID ) ;
      PD_RC_CHECK( rc, PDWARNING, "Failed to add sub-context to dummy coord "
                   "context, rc: %d", rc ) ;

   done :
      return rc ;

   error :
      if ( NULL != (*expiredContext) )
      {
         rtnCB->contextDelete( (*expiredContext)->contextID(), cb ) ;
         (*expiredContext) = NULL ;
      }
      goto done ;
   }

   // PD_TRACE_DECLARE_FUNCTION ( SDB_RTNCOGETREPLY, "rtnCoordGetReply" )
   INT32 rtnCoordGetReply ( pmdEDUCB *cb,  REQUESTID_MAP &requestIdMap,
                            REPLY_QUE &replyQue, const SINT32 opCode,
                            BOOLEAN isWaitAll, BOOLEAN clearReplyIfFailed,
                            BOOLEAN needTimeout )
   {
      INT32 rc = SDB_OK ;
      PD_TRACE_ENTRY ( SDB_RTNCOGETREPLY ) ;

      rtnContextCoord * expiredContext = NULL ;
      ossQueue<pmdEDUEvent> tmpQue ;
      REQUESTID_MAP::iterator iterMap ;

      INT64 oprtTimeout = cb->getCoordSession()->getOperationTimeout() ;
      INT64 waitTime = RTN_COORD_RSP_WAIT_TIME ;
      BOOLEAN needInterrupt = FALSE ;

      oprtTimeout = ( oprtTimeout <= 0 || !needTimeout ) ?
                    0x7FFFFFFFFFFFFFFF : oprtTimeout ;

      while ( requestIdMap.size() > 0 )
      {
         pmdEDUEvent pmdEvent ;
         BOOLEAN isGotMsg = FALSE ;

         if ( !isWaitAll && !replyQue.empty() )
         {
            waitTime = RTN_COORD_RSP_WAIT_TIME_QUICK ;
         }
         else
         {
            waitTime = oprtTimeout < waitTime ?
                       oprtTimeout : waitTime ;
         }

         isGotMsg = cb->waitEvent( pmdEvent, waitTime ) ;

         if ( FALSE == isGotMsg )
         {
            oprtTimeout -= waitTime ;
            if ( !isWaitAll && !replyQue.empty() )
            {
               break ;
            }
            else
            {
               if ( cb->isInterrupted() || cb->isForced() )
               {
                  PD_LOG( PDERROR, "Receive reply failed, because the "
                          "session is interrupted" ) ;
                  rc = SDB_APP_INTERRUPT ;
                  goto error ;
               }
               if ( oprtTimeout <= 0 )
               {
                  rc = SDB_TIMEOUT ;
                  needInterrupt = TRUE ;
                  goto error ;
               }
               continue ;
            }
         }

         if ( pmdEvent._eventType != PMD_EDU_EVENT_MSG )
         {
            PD_LOG ( PDWARNING, "Received unknown event(eventType:%d)",
                     pmdEvent._eventType ) ;
            pmdEduEventRelase( pmdEvent, cb ) ;
            pmdEvent.reset () ;
            continue;
         }
         MsgHeader *pReply = (MsgHeader *)(pmdEvent._Data) ;
         if ( NULL == pReply )
         {
            PD_LOG ( PDWARNING, "Received invalid event(data is null)" );
            continue ;
         }

         if ( MSG_COM_REMOTE_DISC == pReply->opCode )
         {
            MsgRouteID routeID;
            routeID.value = pReply->routeID.value;
            if ( cb->isTransNode( routeID ) )
            {
               cb->setTransRC( SDB_COORD_REMOTE_DISC ) ;
               PD_LOG ( PDERROR, "Transaction operation interrupt, "
                        "remote-node[%s] disconnected",
                        routeID2String( routeID ).c_str() ) ;
            }

            iterMap = requestIdMap.begin() ;
            while ( iterMap != requestIdMap.end() )
            {
               if ( iterMap->second.value == routeID.value )
               {
                  break ;
               }
               ++iterMap ;
            }
            if ( iterMap != requestIdMap.end() &&
                 iterMap->first <= pReply->requestID )
            {
               PD_LOG ( PDERROR, "Get reply failed, remote-node[%s] "
                        "disconnected",
                        routeID2String( iterMap->second ).c_str() ) ;

               MsgHeader *pDiscMsg = NULL ;
               pDiscMsg = (MsgHeader *)SDB_OSS_MALLOC( pReply->messageLength ) ;
               if ( NULL == pDiscMsg )
               {
                  rc = SDB_OOM ;
                  PD_LOG( PDERROR, "Malloc failed(size=%d)",
                          pReply->messageLength ) ;
                  goto error ;
               }
               else
               {
                  ossMemcpy( (CHAR *)pDiscMsg, (CHAR *)pReply,
                              pReply->messageLength );
                  replyQue.push( (CHAR *)pDiscMsg ) ;
               }
               cb->getCoordSession()->delRequest( iterMap->first ) ;
               requestIdMap.erase( iterMap ) ;
            }
            if ( cb->getCoordSession()->isValidResponse( routeID,
                                                         pReply->requestID ) )
            {
               tmpQue.push( pmdEvent );
            }
            else
            {
               SDB_OSS_FREE ( pmdEvent._Data ) ;
               pmdEvent.reset () ;
            }
            continue ;
         } // if ( MSG_COOR_REMOTE_DISC == pReply->opCode )

         iterMap = requestIdMap.find( pReply->requestID ) ;
         if ( iterMap == requestIdMap.end() )
         {
            if ( cb->getCoordSession()->isValidResponse( pReply->requestID ) )
            {
               tmpQue.push( pmdEvent ) ;
            }
            else
            {
               PD_LOG ( PDWARNING, "Received expired or unexpected msg( "
                        "opCode=[%d]%d, expectOpCode=[%d]%d, requestID=%lld, "
                        "TID=%d) from node[%s]",
                        IS_REPLY_TYPE( pReply->opCode ),
                        GET_REQUEST_TYPE( pReply->opCode ),
                        IS_REPLY_TYPE( opCode ),
                        GET_REQUEST_TYPE( opCode ),
                        pReply->requestID, pReply->TID,
                        routeID2String( pReply->routeID ).c_str() ) ;
               _rtnCoordProcessExpiredReply( cb, pReply, &expiredContext ) ;
               SDB_OSS_FREE( pReply ) ;
            }
         }
         else
         {
            if ( opCode != pReply->opCode ||
                 pReply->routeID.value != iterMap->second.value )
            {
               PD_LOG ( PDWARNING, "Received unexpected msg(opCode=[%d]%d,"
                        "requestID=%lld, TID=%d, expectOpCode=[%d]%d, "
                        "expectNode=%s) from node[%s]",
                        IS_REPLY_TYPE( pReply->opCode ),
                        GET_REQUEST_TYPE( pReply->opCode ),
                        pReply->requestID, pReply->TID,
                        IS_REPLY_TYPE( opCode ),
                        GET_REQUEST_TYPE( opCode ),
                        routeID2String( iterMap->second ).c_str(),
                        routeID2String( pReply->routeID ).c_str() ) ;
            }
            else
            {
               PD_LOG ( PDDEBUG , "Received the reply(opCode=[%d]%d, "
                        "requestID=%llu, TID=%u) from node[%s]",
                        IS_REPLY_TYPE( pReply->opCode ),
                        GET_REQUEST_TYPE( pReply->opCode ),
                        pReply->requestID, pReply->TID,
                        routeID2String( pReply->routeID ).c_str() ) ;
            }

            requestIdMap.erase( iterMap ) ;
            cb->getCoordSession()->delRequest( pReply->requestID ) ;
            replyQue.push( (CHAR *)( pmdEvent._Data ) ) ;
            pmdEvent.reset () ;
         } // if ( iterMap == requestIdMap.end() )
      } // while ( requestIdMap.size() > 0 )

      if ( rc )
      {
         goto error;
      }

   done:
      while( !tmpQue.empty() )
      {
         pmdEDUEvent otherEvent;
         tmpQue.wait_and_pop( otherEvent );
         cb->postEvent( otherEvent );
      }
      if ( NULL != expiredContext )
      {
         SDB_RTNCB * rtnCB = pmdGetKRCB()->getRTNCB() ;
         rtnCB->contextDelete( expiredContext->contextID(), cb ) ;
      }
      PD_TRACE_EXITRC ( SDB_RTNCOGETREPLY, rc ) ;
      return rc;

   error:
      if ( clearReplyIfFailed )
      {
         while ( !replyQue.empty() )
         {
            CHAR *pData = replyQue.front();
            replyQue.pop();
            SDB_OSS_FREE( pData );
         }
      }
      rtnCoordClearRequest( cb, requestIdMap, needInterrupt ) ;
      goto done;
   }

   // PD_TRACE_DECLARE_FUNCTION ( SDB_GETSERVNAME, "getServiceName" )
   INT32 getServiceName ( BSONElement &beService,
                          INT32 serviceType,
                          std::string &strServiceName )
   {
      INT32 rc = SDB_OK;
      PD_TRACE_ENTRY ( SDB_GETSERVNAME ) ;
      strServiceName = "";
      do
      {
         if ( beService.type() != Array )
         {
            rc = SDB_INVALIDARG;
            PD_LOG ( PDERROR,
                     "failed to get the service-name(type=%d),\
                     the service field is invalid", serviceType );
            break;
         }
         try
         {
            BSONObjIterator i( beService.embeddedObject() );
            while ( i.more() )
            {
               BSONElement beTmp = i.next();
               BSONObj boTmp = beTmp.embeddedObject();
               BSONElement beServiceType = boTmp.getField(
                  CAT_SERVICE_TYPE_FIELD_NAME );
               if ( beServiceType.eoo() || !beServiceType.isNumber() )
               {
                  rc = SDB_INVALIDARG;
                  PD_LOG ( PDERROR, "Failed to get the service-name(type=%d),"
                           "get the field(%s) failed", serviceType,
                           CAT_SERVICE_TYPE_FIELD_NAME );
                  break;
               }
               if ( beServiceType.numberInt() == serviceType )
               {
                  BSONElement beServiceName = boTmp.getField(
                     CAT_SERVICE_NAME_FIELD_NAME );
                  if ( beServiceType.eoo() || beServiceName.type() != String )
                  {
                     rc = SDB_INVALIDARG;
                     PD_LOG ( PDERROR, "Failed to get the service-name"
                              "(type=%d), get the field(%s) failed",
                              serviceType, CAT_SERVICE_NAME_FIELD_NAME ) ;
                     break;
                  }
                  strServiceName = beServiceName.String();
                  break;
               }
            }
         }
         catch ( std::exception &e )
         {
            PD_LOG ( PDERROR, "unexpected exception: %s", e.what() ) ;
            rc = SDB_INVALIDARG ;
         }
      }while ( FALSE );
      PD_TRACE_EXITRC ( SDB_GETSERVNAME, rc ) ;
      return rc;
   }

   // PD_TRACE_DECLARE_FUNCTION ( SDB_RTNCOGETCATAINFO, "rtnCoordGetCataInfo" )
   INT32 rtnCoordGetCataInfo( pmdEDUCB *cb,
                              const CHAR *pCollectionName,
                              BOOLEAN isNeedRefreshCata,
                              CoordCataInfoPtr &cataInfo,
                              BOOLEAN *pHasUpdate )
   {
      INT32 rc = SDB_OK;
      PD_TRACE_ENTRY ( SDB_RTNCOGETCATAINFO ) ;
      while( TRUE )
      {
         if ( isNeedRefreshCata )
         {
            rc = rtnCoordGetRemoteCata( cb, pCollectionName, cataInfo ) ;
            if ( SDB_OK == rc && pHasUpdate )
            {
               *pHasUpdate = TRUE ;
            }
         }
         else
         {
            rc = rtnCoordGetLocalCata ( pCollectionName, cataInfo );
            if ( SDB_CAT_NO_MATCH_CATALOG == rc )
            {
               isNeedRefreshCata = TRUE ;
               continue;
            }
         }
         break;
      }
      PD_TRACE_EXITRC ( SDB_RTNCOGETCATAINFO, rc ) ;
      return rc ;
   }

   void rtnCoordRemoveGroup( UINT32 group )
   {
      pmdGetKRCB()->getCoordCB()->removeGroupInfo( group ) ;
   }

   // PD_TRACE_DECLARE_FUNCTION ( SDB_RTNCOGETLOCALCATA, "rtnCoordGetLocalCata" )
   INT32 rtnCoordGetLocalCata( const CHAR *pCollectionName,
                               CoordCataInfoPtr &cataInfo )
   {
      INT32 rc = SDB_OK;
      PD_TRACE_ENTRY ( SDB_RTNCOGETLOCALCATA ) ;
      pmdKRCB *pKrcb          = pmdGetKRCB();
      CoordCB *pCoordcb       = pKrcb->getCoordCB();
      rc = pCoordcb->getCataInfo( pCollectionName, cataInfo ) ;
      PD_TRACE_EXITRC ( SDB_RTNCOGETLOCALCATA, rc ) ;
      return rc;
   }

   // PD_TRACE_DECLARE_FUNCTION ( SDB_RTNCOGETREMOTECATA, "rtnCoordGetRemoteCata" )
   INT32 rtnCoordGetRemoteCata( pmdEDUCB *cb,
                                const CHAR *pCollectionName,
                                CoordCataInfoPtr &cataInfo,
                                BOOLEAN withSubCL )
   {
      INT32 rc = SDB_OK;
      PD_TRACE_ENTRY ( SDB_RTNCOGETREMOTECATA ) ;
      pmdKRCB *pKrcb                   = pmdGetKRCB();
      CoordCB *pCoordcb                = pKrcb->getCoordCB();
      netMultiRouteAgent *pRouteAgent  = pCoordcb->getRouteAgent();
      CoordGroupInfoPtr cataGroupInfo ;
      MsgRouteID nodeID ;
      UINT32 primaryID = 0 ;
      UINT32 times = 0 ;

      rc = rtnCoordGetCatGroupInfo( cb, FALSE, cataGroupInfo, NULL ) ;
      if ( rc != SDB_OK )
      {
         PD_LOG ( PDERROR, "Failed to get the catalogue-node-group "
                  "info, rc: %d", rc ) ;
         goto error ;
      }

      do
      {
         BSONObj boQuery;
         BSONObj boFieldSelector;
         BSONObj boOrderBy;
         BSONObj boHint;
         SINT64 numToReturn ;
         try
         {
            if ( withSubCL )
            {
               boQuery = OR( BSON( CAT_CATALOGNAME_NAME << pCollectionName ),
                             BSON( CAT_MAINCL_NAME << pCollectionName ) ) ;
               numToReturn = -1 ;
            }
            else
            {
               boQuery = BSON( CAT_CATALOGNAME_NAME << pCollectionName ) ;
               numToReturn = 1 ;
            }
         }
         catch ( std::exception &e )
         {
            rc = SDB_SYS;
            PD_LOG ( PDERROR, "Get remote catalogue failed, while "
                     "build query-obj received unexception error:%s",
                     e.what() );
            break ;
         }
         CHAR *pBuffer = NULL;
         INT32 bufferSize = 0;
         rc = msgBuildQueryCatalogReqMsg ( &pBuffer, &bufferSize,
                                           0, 0, 0, numToReturn, cb->getTID(),
                                           &boQuery, &boFieldSelector,
                                           &boOrderBy, &boHint );
         if ( rc != SDB_OK )
         {
            PD_LOG ( PDERROR, "Failed to build query-catalog-message, rc: %d",
                     rc );
            break ;
         }

         REQUESTID_MAP sendNodes;
         rc = rtnCoordSendRequestToPrimary( pBuffer, cataGroupInfo, sendNodes,
                                            pRouteAgent, MSG_ROUTE_CAT_SERVICE,
                                            cb ) ;
         if ( pBuffer != NULL )
         {
            SDB_OSS_FREE( pBuffer );
         }
         if ( rc != SDB_OK )
         {
            rtnCoordClearRequest( cb, sendNodes ) ;

            PD_LOG ( PDERROR, "Failed to send catalog-query msg to "
                     "catalogue-group, rc: %d", rc ) ;
            break ;
         }

         REPLY_QUE replyQue;
         rc = rtnCoordGetReply( cb, sendNodes, replyQue,
                                MSG_CAT_QUERY_CATALOG_RSP ) ;
         if ( rc )
         {
            PD_LOG( PDERROR, "Failed to get reply from catalogue-node, "
                    "rc: %d", rc ) ;
            break ;
         }

         BOOLEAN isGetExpectReply = FALSE ;
         while ( !replyQue.empty() )
         {
            MsgCatQueryCatRsp *pReply = NULL ;
            pReply = (MsgCatQueryCatRsp *)(replyQue.front());
            replyQue.pop();

            if ( FALSE == isGetExpectReply )
            {
               nodeID.value = pReply->header.routeID.value ;
               primaryID = pReply->startFrom ;
               rc = rtnCoordProcessQueryCatReply( pReply,
                                                  pCollectionName,
                                                  cataInfo ) ;
               if( SDB_OK == rc )
               {
                  isGetExpectReply = TRUE ;
               }
            }
            if ( NULL != pReply )
            {
               SDB_OSS_FREE( pReply ) ;
            }
         }

         if ( rc )
         {
            if ( rtnCoordGroupReplyCheck( cb, rc, rtnCoordCanRetry( times++ ),
                                          nodeID, cataGroupInfo, NULL, TRUE,
                                          primaryID, TRUE ) )
            {
               continue ;
            }

            PD_LOG( PDERROR, "Get catalog info[%s] from remote failed, rc: %d",
                    pCollectionName, rc ) ;

            if ( ( SDB_DMS_NOTEXIST == rc || SDB_DMS_EOC == rc ) &&
                 pCoordcb->isSubCollection( pCollectionName ) )
            {
               rc = SDB_CLS_COORD_NODE_CAT_VER_OLD ;
            }
         }
         break ;
      }while ( TRUE ) ;

      if ( SDB_OK == rc && !withSubCL &&
           cataInfo.get() && cataInfo->isMainCL() &&
           cataInfo->getSubCLCount() > 0 )
      {
         CoordCataInfoPtr updatedCataInfo ;
         const CHAR *pCLName = cataInfo->getName() ;
         rc = rtnCoordGetRemoteCata( cb, pCLName, updatedCataInfo, TRUE ) ;
         if ( SDB_OK != rc )
         {
            PD_LOG( PDWARNING, "Get main collection[%s]'s sub collections "
                    "failed, rc: %d", pCLName,
                    rc ) ;
            pCoordcb->delCataInfo( pCLName ) ;
            goto error ;
         }
         cataInfo = updatedCataInfo ;
      }

   done:
      PD_TRACE_EXITRC ( SDB_RTNCOGETREMOTECATA, rc ) ;
      return rc ;
   error:
      goto done ;
   }

   // PD_TRACE_DECLARE_FUNCTION ( SDB_RTNCOGETGROUPINFO, "rtnCoordGetGroupInfo" )
   INT32 rtnCoordGetGroupInfo ( pmdEDUCB *cb,
                                UINT32 groupID,
                                BOOLEAN isNeedRefresh,
                                CoordGroupInfoPtr &groupInfo,
                                BOOLEAN *pHasUpdate )
   {
      INT32 rc = SDB_OK;
      PD_TRACE_ENTRY ( SDB_RTNCOGETGROUPINFO ) ;

      while( TRUE )
      {
         if ( isNeedRefresh )
         {
            rc = rtnCoordGetRemoteGroupInfo( cb, groupID, NULL, groupInfo ) ;
            if ( SDB_OK == rc && pHasUpdate )
            {
               *pHasUpdate = TRUE ;
            }
         }
         else
         {
            rc = rtnCoordGetLocalGroupInfo ( groupID, groupInfo );
            if ( SDB_COOR_NO_NODEGROUP_INFO == rc )
            {
               isNeedRefresh = TRUE ;
               continue;
            }
         }
         break ;
      }

      PD_TRACE_EXITRC ( SDB_RTNCOGETGROUPINFO, rc ) ;

      return rc;
   }

   INT32 rtnCoordGetGroupInfo( pmdEDUCB *cb,
                               const CHAR *groupName,
                               BOOLEAN isNeedRefresh,
                               CoordGroupInfoPtr &groupInfo,
                               BOOLEAN *pHasUpdate )
   {
      INT32 rc = SDB_OK;

      while( TRUE )
      {
         if ( isNeedRefresh )
         {
            rc = rtnCoordGetRemoteGroupInfo( cb, 0, groupName, groupInfo ) ;
            if ( SDB_OK == rc && pHasUpdate )
            {
               *pHasUpdate = TRUE ;
            }
         }
         else
         {
            rc = rtnCoordGetLocalGroupInfo ( groupName, groupInfo ) ;
            if ( SDB_COOR_NO_NODEGROUP_INFO == rc )
            {
               isNeedRefresh = TRUE ;
               continue ;
            }
         }
         break ;
      }

      return rc;
   }

   // PD_TRACE_DECLARE_FUNCTION ( SDB_RTNCOGETLOCALGROUPINFO, "rtnCoordGetLocalGroupInfo" )
   INT32 rtnCoordGetLocalGroupInfo ( UINT32 groupID,
                                     CoordGroupInfoPtr &groupInfo )
   {
      INT32 rc = SDB_OK;
      PD_TRACE_ENTRY ( SDB_RTNCOGETLOCALGROUPINFO ) ;
      pmdKRCB *pKrcb          = pmdGetKRCB();
      CoordCB *pCoordcb       = pKrcb->getCoordCB();

      if ( CATALOG_GROUPID == groupID )
      {
         groupInfo = pCoordcb->getCatGroupInfo() ;
      }
      else
      {
         rc = pCoordcb->getGroupInfo( groupID, groupInfo ) ;
      }
      PD_TRACE_EXITRC ( SDB_RTNCOGETLOCALGROUPINFO, rc ) ;
      return rc ;
   }

   INT32 rtnCoordGetLocalGroupInfo ( const CHAR *groupName,
                                     CoordGroupInfoPtr &groupInfo )
   {
      INT32 rc = SDB_OK ;
      pmdKRCB *pKrcb          = pmdGetKRCB() ;
      CoordCB *pCoordcb       = pKrcb->getCoordCB() ;
      if ( 0 == ossStrcmp( CATALOG_GROUPNAME, groupName ) )
      {
         groupInfo = pCoordcb->getCatGroupInfo() ;
      }
      else
      {
         rc = pCoordcb->getGroupInfo( groupName, groupInfo ) ;
      }
      return rc ;
   }

   // PD_TRACE_DECLARE_FUNCTION ( SDB_RTNCOGETREMOTEGROUPINFO, "rtnCoordGetRemoteGroupInfo" )
   INT32 rtnCoordGetRemoteGroupInfo ( pmdEDUCB *cb,
                                      UINT32 groupID,
                                      const CHAR *groupName,
                                      CoordGroupInfoPtr &groupInfo,
                                      BOOLEAN addToLocal )
   {
      INT32 rc = SDB_OK ;
      PD_TRACE_ENTRY ( SDB_RTNCOGETREMOTEGROUPINFO ) ;
      pmdKRCB *pKrcb          = pmdGetKRCB();
      CoordCB *pCoordcb       = pKrcb->getCoordCB();
      netMultiRouteAgent *pRouteAgent  = pCoordcb->getRouteAgent();
      CHAR *buf = NULL ;
      MsgCatGroupReq *msg = NULL ;
      MsgCatGroupReq msgGroupReq ;
      CoordGroupInfoPtr cataGroupInfo ;
      MsgRouteID nodeID ;
      UINT32 times= 0 ;

      if ( CATALOG_GROUPID == groupID ||
           ( groupName && 0 == ossStrcmp( groupName, CATALOG_GROUPNAME ) ) )
      {
         rc = rtnCoordGetRemoteCatGroupInfo( cb, groupInfo ) ;
         goto done ;
      }

      rc = rtnCoordGetCatGroupInfo( cb, FALSE, cataGroupInfo, NULL );
      if ( rc != SDB_OK )
      {
         PD_LOG ( PDERROR, "Failed to get cata-group-info,"
                  "no catalogue-node-group info" );
        goto error ;
      }

      if ( NULL == groupName )
      {
         msgGroupReq.id.columns.groupID = groupID;
         msgGroupReq.id.columns.nodeID = 0;
         msgGroupReq.id.columns.serviceID = 0;
         msgGroupReq.header.messageLength = sizeof( MsgCatGroupReq );
         msgGroupReq.header.opCode = MSG_CAT_GRP_REQ;
         msgGroupReq.header.routeID.value = 0;
         msgGroupReq.header.TID = cb->getTID();
         msg = &msgGroupReq ;
      }
      else
      {
         UINT32 nameLen = ossStrlen( groupName ) + 1 ;
         UINT32 msgLen = nameLen +  sizeof(MsgCatGroupReq) ;
         buf = ( CHAR * )SDB_OSS_MALLOC( msgLen ) ;
         if ( NULL == buf )
         {
            PD_LOG( PDERROR, "failed to allocate mem." ) ;
            rc = SDB_OOM ;
            goto error ;
         }
         msg = ( MsgCatGroupReq * )buf ;
         msg->id.value = 0 ;
         msg->header.messageLength = msgLen ;
         msg->header.opCode = MSG_CAT_GRP_REQ;
         msg->header.routeID.value = 0;
         msg->header.TID = cb->getTID();
         ossMemcpy( buf + sizeof(MsgCatGroupReq),
                    groupName, nameLen ) ;
      }

      do
      {
         REQUESTID_MAP sendNodes ;
         rc = rtnCoordSendRequestToPrimary( (CHAR *)(msg),
                                            cataGroupInfo, sendNodes,
                                            pRouteAgent,
                                            MSG_ROUTE_CAT_SERVICE,
                                            cb );
         if ( rc != SDB_OK )
         {
            rtnCoordClearRequest( cb, sendNodes ) ;

            if ( groupName )
            {
               PD_LOG ( PDERROR, "Failed to get group info[%s], because of "
                        "send group-info-request failed, rc: %d",
                        groupName, rc ) ;
            }
            else
            {
               PD_LOG ( PDERROR, "Failed to get group info[%u], because of "
                        "send group-info-request failed, rc: %d",
                        groupID, rc ) ;
            }
            break ;
         }

         REPLY_QUE replyQue;
         rc = rtnCoordGetReply( cb, sendNodes, replyQue,
                                MSG_CAT_GRP_RES ) ;
         if ( rc != SDB_OK )
         {
            if ( groupName )
            {
               PD_LOG ( PDWARNING, "Failed to get group info[%s], because of "
                        "get reply failed, rc: %d", groupName, rc ) ;
            }
            else
            {
               PD_LOG ( PDWARNING, "Failed to get group info[%u], because of "
                        "get reply failed, rc: %d", groupID, rc ) ;
            }
            break ;
         }

         BOOLEAN getExpected = FALSE ;
         UINT32 primaryID = 0 ;
         while ( !replyQue.empty() )
         {
            MsgHeader *pReply = NULL;
            pReply = (MsgHeader *)(replyQue.front());
            replyQue.pop() ;

            if ( FALSE == getExpected )
            {
               nodeID.value = pReply->routeID.value ;
               primaryID = MSG_GET_INNER_REPLY_STARTFROM( pReply ) ;
               rc = rtnCoordProcessGetGroupReply( pReply, groupInfo ) ;
               if ( SDB_OK == rc )
               {
                  getExpected = TRUE ;
                  if ( addToLocal )
                  {
                     pCoordcb->addGroupInfo( groupInfo ) ;
                     rc = rtnCoordUpdateRoute( groupInfo, pRouteAgent,
                                               MSG_ROUTE_SHARD_SERVCIE ) ;
                  }
               }
            }
            if ( NULL != pReply )
            {
               SDB_OSS_FREE( pReply );
            }
         }

         if ( rc != SDB_OK )
         {
            if ( rtnCoordGroupReplyCheck( cb, rc, rtnCoordCanRetry( times++ ),
                                          nodeID, cataGroupInfo, NULL, TRUE,
                                          primaryID, TRUE ) )
            {
               continue;
            }

            if ( groupName )
            {
               PD_LOG ( PDERROR, "Failed to get group info[%s], because of "
                        "reply error, flag: %d", groupName, rc ) ;
            }
            else
            {
               PD_LOG ( PDERROR, "Failed to get group info[%u], because of "
                        "reply error, flag: %d", groupID, rc ) ;
            }
         }
         break ;
      }while ( TRUE ) ;

   done:
      if ( NULL != buf )
      {
         SDB_OSS_FREE( buf ) ;
         buf = NULL ;
      }
      PD_TRACE_EXITRC ( SDB_RTNCOGETREMOTEGROUPINFO, rc ) ;
      return rc;
   error:
      goto done ;
   }

   // PD_TRACE_DECLARE_FUNCTION ( SDB_RTNCOGETCATGROUPINFO, "rtnCoordGetCatGroupInfo" )
   INT32 rtnCoordGetCatGroupInfo ( pmdEDUCB *cb,
                                   BOOLEAN isNeedRefresh,
                                   CoordGroupInfoPtr &groupInfo,
                                   BOOLEAN *pHasUpdate )
   {
      INT32 rc = SDB_OK;
      PD_TRACE_ENTRY ( SDB_RTNCOGETCATGROUPINFO ) ;
      UINT32 retryCount = 0 ;

      while( TRUE )
      {
         if ( isNeedRefresh )
         {
            rc = rtnCoordGetRemoteCatGroupInfo( cb, groupInfo ) ;
            if ( SDB_CLS_GRP_NOT_EXIST == rc && retryCount < 30 )
            {
               ++retryCount ;
               ossSleep( 100 ) ;
               continue ;
            }
            if ( SDB_OK == rc && pHasUpdate )
            {
               *pHasUpdate = TRUE ;
            }
         }
         else
         {
            rc = rtnCoordGetLocalCatGroupInfo ( groupInfo ) ;
            if ( ( SDB_OK == rc && groupInfo->nodeCount() == 0 ) ||
                   SDB_COOR_NO_NODEGROUP_INFO == rc )
            {
               isNeedRefresh = TRUE;
               continue ;
            }
         }
         break;
      }
      PD_TRACE_EXITRC ( SDB_RTNCOGETCATGROUPINFO, rc ) ;
      return rc ;
   }

   // PD_TRACE_DECLARE_FUNCTION ( SDB_RTNCOGETLOCALCATGROUPINFO, "rtnCoordGetLocalCatGroupInfo" )
   INT32 rtnCoordGetLocalCatGroupInfo ( CoordGroupInfoPtr &groupInfo )
   {
      INT32 rc = SDB_OK;
      PD_TRACE_ENTRY ( SDB_RTNCOGETLOCALCATGROUPINFO ) ;
      pmdKRCB *pKrcb          = pmdGetKRCB();
      CoordCB *pCoordcb       = pKrcb->getCoordCB();
      groupInfo = pCoordcb->getCatGroupInfo();
      PD_TRACE_EXITRC ( SDB_RTNCOGETLOCALCATGROUPINFO, rc ) ;
      return rc;
   }

   // PD_TRACE_DECLARE_FUNCTION ( SDB_RTNCOGETREMOTECATAGROUPINFOBYADDR, "rtnCoordGetRemoteCataGroupInfoByAddr" )
   INT32 rtnCoordGetRemoteCataGroupInfoByAddr ( pmdEDUCB *cb,
                                                CoordGroupInfoPtr &groupInfo )
   {
      INT32 rc = SDB_OK;
      PD_TRACE_ENTRY ( SDB_RTNCOGETREMOTECATAGROUPINFOBYADDR ) ;
      UINT64 reqID;
      pmdKRCB *pKrcb                   = pmdGetKRCB();
      CoordCB *pCoordcb                = pKrcb->getCoordCB();
      netMultiRouteAgent *pRouteAgent  = pCoordcb->getRouteAgent();
      MsgHeader *pReply                = NULL;
      ossQueue<pmdEDUEvent> tmpQue ;
      UINT32 sendPos                   = 0 ;

      MsgCatCatGroupReq msgGroupReq;
      msgGroupReq.id.columns.groupID = CAT_CATALOG_GROUPID ;
      msgGroupReq.id.columns.nodeID = 0;
      msgGroupReq.id.columns.serviceID = 0;
      msgGroupReq.header.messageLength = sizeof( MsgCatGroupReq );
      msgGroupReq.header.opCode = MSG_CAT_GRP_REQ ;
      msgGroupReq.header.routeID.value = 0;
      msgGroupReq.header.TID = cb->getTID();

      CoordVecNodeInfo cataNodeAddrList ;
      pCoordcb->getCatNodeAddrList( cataNodeAddrList ) ;

      if ( cataNodeAddrList.size() == 0 )
      {
         rc = SDB_CAT_NO_ADDR_LIST ;
         PD_LOG ( PDERROR, "no catalog node info" );
         goto error ;
      }

      while( sendPos < cataNodeAddrList.size() )
      {
         BOOLEAN disconnected = FALSE ;

         for ( ; sendPos < cataNodeAddrList.size() ; ++sendPos )
         {
            rc = pRouteAgent->syncSendWithoutCheck( cataNodeAddrList[sendPos]._id,
                                                    (MsgHeader*)&msgGroupReq,
                                                    reqID, cb ) ;
            if ( SDB_OK == rc )
            {
               break ;
            }
         }
         if ( rc != SDB_OK )
         {
            PD_LOG ( PDERROR, "Failed to send the request to catalog-node"
                     "(rc=%d)", rc );
            break ;
         }
         while ( TRUE )
         {
            pmdEDUEvent pmdEvent;
            BOOLEAN isGotMsg = cb->waitEvent( pmdEvent,
                                              RTN_COORD_RSP_WAIT_TIME ) ;
            if ( cb->isForced() ||
                 ( cb->isInterrupted() && !( cb->isDisconnected() ) ) )
            {
               if ( isGotMsg )
               {
                  pmdEduEventRelase( pmdEvent, cb ) ;
               }
               rc = SDB_APP_INTERRUPT ;
               break ;
            }

            if ( FALSE == isGotMsg )
            {
               continue ;
            }
            if ( PMD_EDU_EVENT_MSG != pmdEvent._eventType )
            {
               PD_LOG ( PDWARNING, "Received unknown event(eventType=%d)",
                        pmdEvent._eventType ) ;
               pmdEduEventRelase( pmdEvent, cb ) ;
               pmdEvent.reset () ;
               continue ;
            }
            MsgHeader *pMsg = (MsgHeader *)(pmdEvent._Data) ;
            if ( NULL == pMsg )
            {
               PD_LOG ( PDWARNING, "Received invalid msg-event(data is null)" );
               continue ;
            }

            if ( MSG_COM_REMOTE_DISC == pMsg->opCode )
            {
               PD_LOG ( PDERROR, "Get reply failed, remote-node[%s] disconnected",
                        routeID2String( pMsg->routeID ).c_str() ) ;


               if ( reqID <= pMsg->requestID )
               {
                  cb->getCoordSession()->delRequest( reqID ) ;
                  disconnected = TRUE ;
               }

               if ( cb->getCoordSession()->isValidResponse( pMsg->routeID,
                                                            pMsg->requestID ) )
               {
                  tmpQue.push( pmdEvent ) ;
               }
               else
               {
                  SDB_OSS_FREE( pmdEvent._Data ) ;
                  pmdEvent.reset () ;
               }

               if ( disconnected )
               {
                  break ;
               }
               else
               {
                  continue ;
               }
            }

            if ( pMsg->opCode != MSG_CAT_GRP_RES ||
                 pMsg->requestID != reqID )
            {
               if ( cb->getCoordSession()->isValidResponse( reqID ) )
               {
                  tmpQue.push( pmdEvent );
               }
               else
               {
                  PD_LOG( PDWARNING, "Received unexpected message"
                        "(opCode=[%d]%d, requestID=%llu, TID=%u, "
                        "groupID=%u, nodeID=%u, serviceID=%u )",
                        IS_REPLY_TYPE( pMsg->opCode ),
                        GET_REQUEST_TYPE( pMsg->opCode ),
                        pMsg->requestID, pMsg->TID,
                        pMsg->routeID.columns.groupID,
                        pMsg->routeID.columns.nodeID,
                        pMsg->routeID.columns.serviceID );
                  SDB_OSS_FREE( pMsg ) ;
               }
               continue ;
            }
            cb->getCoordSession()->delRequest( reqID ) ;
            pReply = (MsgHeader *)pMsg ;
            break ;
         }

         if ( disconnected )
         {
            rc = SDB_NETWORK_CLOSE ;
            ++sendPos ;
            continue ;
         }

         if ( rc != SDB_OK || NULL == pReply )
         {
            PD_LOG ( PDERROR, "Failed to get reply(rc=%d)", rc );
            break ;
         }
         rc = rtnCoordProcessGetGroupReply( pReply, groupInfo ) ;
         if ( rc != SDB_OK )
         {
            PD_LOG ( PDERROR,
                     "Failed to get catalog-group info, reply error(rc=%d)",
                     rc ) ;
            SDB_OSS_FREE( pReply ) ;
            pReply = NULL ;
            continue ;
         }
         rc = rtnCoordUpdateRoute( groupInfo, pRouteAgent,
                                   MSG_ROUTE_CAT_SERVICE ) ;
         if ( rc != SDB_OK )
         {
            PD_LOG ( PDERROR, "Failed to get catalog-group info,"
                     "update route failed(rc=%d)", rc );
            ++sendPos ;
            SDB_OSS_FREE( pReply ) ;
            pReply = NULL ;
            continue ;
         }

         rtnCoordUpdateRoute( groupInfo, pRouteAgent,
                              MSG_ROUTE_SHARD_SERVCIE ) ;

         pCoordcb->addGroupInfo( groupInfo ) ;
         pCoordcb->updateCatGroupInfo( groupInfo ) ;

         break ;
      }

   done:
      if ( NULL != pReply )
      {
         SDB_OSS_FREE( pReply ) ;
      }
      while ( !tmpQue.empty() )
      {
         pmdEDUEvent otherEvent;
         tmpQue.wait_and_pop( otherEvent );
         cb->postEvent( otherEvent );
      }
      PD_TRACE_EXITRC ( SDB_RTNCOGETREMOTECATAGROUPINFOBYADDR, rc ) ;
      return rc ;
   error:
      goto done ;
   }

   // PD_TRACE_DECLARE_FUNCTION ( SDB_RTNCOGETREMOTECATGROUPINFO, "rtnCoordGetRemoteCatGroupInfo" )
   INT32 rtnCoordGetRemoteCatGroupInfo ( pmdEDUCB *cb,
                                         CoordGroupInfoPtr &groupInfo )
   {
      INT32 rc = SDB_OK;
      PD_TRACE_ENTRY ( SDB_RTNCOGETREMOTECATGROUPINFO ) ;
      pmdKRCB *pKrcb                   = pmdGetKRCB();
      CoordCB *pCoordcb                = pKrcb->getCoordCB();
      netMultiRouteAgent *pRouteAgent  = pCoordcb->getRouteAgent();

      MsgRouteID nodeID ;
      CoordGroupInfoPtr cataGroupInfo ;
      rtnCoordGetLocalCatGroupInfo( cataGroupInfo ) ;
      UINT32 times = 0 ;

      MsgCatCatGroupReq msgGroupReq;
      msgGroupReq.id.columns.groupID = CAT_CATALOG_GROUPID;
      msgGroupReq.id.columns.nodeID = 0;
      msgGroupReq.id.columns.serviceID = 0;
      msgGroupReq.header.messageLength = sizeof( MsgCatGroupReq );
      msgGroupReq.header.opCode = MSG_CAT_GRP_REQ ;

      if ( cataGroupInfo->nodeCount() == 0 )
      {
         rc = rtnCoordGetRemoteCataGroupInfoByAddr( cb, groupInfo ) ;
         if ( rc != SDB_OK )
         {
            PD_LOG ( PDERROR, "Failed to get cata-group-info by addr, "
                     "rc: %d", rc ) ;
            goto error ;
         }
         goto done ;
      }

      do
      {
         msgGroupReq.header.routeID.value = 0;
         msgGroupReq.header.TID = cb->getTID();
         REQUESTID_MAP sendNodes ;
         rc = rtnCoordSendRequestToOne( (CHAR *)(&msgGroupReq),
                                        cataGroupInfo, sendNodes,
                                        pRouteAgent, MSG_ROUTE_CAT_SERVICE,
                                        cb, FALSE ) ;
         if ( rc != SDB_OK )
         {
            rtnCoordClearRequest( cb, sendNodes );
            PD_LOG ( PDERROR, "Failed to get cata-group-info from "
                     "catalogue-node,send group-info-request failed, rc: %d",
                     rc ) ;
            break ;
         }

         REPLY_QUE replyQue;
         rc = rtnCoordGetReply( cb, sendNodes, replyQue,
                                MSG_CAT_GRP_RES ) ;
         if ( rc != SDB_OK )
         {
            PD_LOG ( PDWARNING, "Failed to get cata-group-info from "
                     "catalogue-node, get reply failed, rc: %d", rc );
            break ;
         }

         BOOLEAN getExpected = FALSE ;
         UINT32 primaryID = 0 ;
         while ( !replyQue.empty() )
         {
            MsgHeader *pReply = NULL;
            pReply = (MsgHeader *)(replyQue.front());
            replyQue.pop();

            if ( FALSE == getExpected )
            {
               nodeID.value = pReply->routeID.value ;
               primaryID = MSG_GET_INNER_REPLY_STARTFROM(pReply) ;
               rc = rtnCoordProcessGetGroupReply( pReply, groupInfo ) ;
               if ( SDB_OK == rc )
               {
                  getExpected = TRUE ;
                  pCoordcb->addGroupInfo( groupInfo ) ;
                  pCoordcb->updateCatGroupInfo( groupInfo );
                  rc = rtnCoordUpdateRoute( groupInfo,  pRouteAgent,
                                            MSG_ROUTE_CAT_SERVICE ) ;
                  rtnCoordUpdateRoute( groupInfo, pRouteAgent,
                                       MSG_ROUTE_SHARD_SERVCIE ) ;
               }
            }
            if ( NULL != pReply )
            {
               SDB_OSS_FREE( pReply );
            }
         }

         if ( rc != SDB_OK )
         {
            if ( rtnCoordGroupReplyCheck( cb, rc, rtnCoordCanRetry( times++ ),
                                          nodeID, cataGroupInfo, NULL,
                                          TRUE, primaryID, TRUE ) )
            {
               continue ;
            }
            PD_LOG( PDERROR, "Get catalog group info from remote failed, "
                    "rc: %d", rc ) ;
         }
         break ;
      }while ( TRUE ) ;

   done:
      PD_TRACE_EXITRC ( SDB_RTNCOGETREMOTECATGROUPINFO, rc ) ;
      return rc ;
   error:
      goto done ;
   }

   // PD_TRACE_DECLARE_FUNCTION ( SDB_RTNCOUPDATEROUTE, "rtnCoordUpdateRoute" )
   INT32 rtnCoordUpdateRoute ( CoordGroupInfoPtr &groupInfo,
                               netMultiRouteAgent *pRouteAgent,
                               MSG_ROUTE_SERVICE_TYPE type )
   {
      INT32 rc = SDB_OK;
      PD_TRACE_ENTRY ( SDB_RTNCOUPDATEROUTE ) ;

      string host ;
      string service ;
      MsgRouteID routeID ;
      routeID.value = MSG_INVALID_ROUTEID ;

      UINT32 index = 0 ;
      clsGroupItem *groupItem = groupInfo.get() ;
      while ( SDB_OK == groupItem->getNodeInfo( index++, routeID, host,
                                                service, type ) )
      {
         rc = pRouteAgent->updateRoute( routeID, host.c_str(),
                                        service.c_str() ) ;
         if ( rc != SDB_OK )
         {
            if ( SDB_NET_UPDATE_EXISTING_NODE == rc )
            {
               rc = SDB_OK;
            }
            else
            {
               PD_LOG ( PDERROR, "update route failed (rc=%d)", rc ) ;
               break;
            }
         }
      }
      PD_TRACE_EXITRC ( SDB_RTNCOUPDATEROUTE, rc ) ;
      return rc;
   }

   // PD_TRACE_DECLARE_FUNCTION ( SDB_RTNCOGETGROUPSBYCATAINFO, "rtnCoordGetGroupsByCataInfo" )
   INT32 rtnCoordGetGroupsByCataInfo( CoordCataInfoPtr &cataInfo,
                                      CoordGroupList &sendGroupLst,
                                      CoordGroupList &groupLst,
                                      pmdEDUCB *cb,
                                      BOOLEAN *pHasUpdate,
                                      const BSONObj *pQuery )
   {
      INT32 rc = SDB_OK ;
      PD_TRACE_ENTRY ( SDB_RTNCOGETGROUPSBYCATAINFO ) ;
      if ( !cataInfo->isMainCL() )
      {
         if ( NULL == pQuery || pQuery->isEmpty() )
         {
            cataInfo->getGroupLst( groupLst ) ;
         }
         else
         {
            rc = cataInfo->getGroupByMatcher( *pQuery, groupLst ) ;
            PD_RC_CHECK( rc, PDWARNING,
                         "Failed to get group by matcher(rc=%d)",
                         rc ) ;
         }

         if ( groupLst.size() <= 0 )
         {
            if ( pQuery )
            {
               PD_LOG( PDWARNING, "Failed to get groups for obj[%s] from "
                       "catalog info[%s]", pQuery->toString().c_str(),
                       cataInfo->getCatalogSet()->toCataInfoBson(
                       ).toString().c_str() ) ;
            }
         }
         else
         {
            CoordGroupList::iterator iter = sendGroupLst.begin();
            while( iter != sendGroupLst.end() )
            {
               groupLst.erase( iter->first );
               ++iter;
            }
         }
      }
      else
      {
         vector< string > subCLLst ;

         if ( NULL == pQuery || pQuery->isEmpty() )
         {
            cataInfo->getSubCLList( subCLLst ) ;
         }
         else
         {
            cataInfo->getMatchSubCLs( *pQuery, subCLLst ) ;
         }

         if ( 0 == subCLLst.size() &&
              ( !pHasUpdate ||
                ( pHasUpdate && !(*pHasUpdate) ) ) )
         {
            CHAR clName[ DMS_COLLECTION_FULL_NAME_SZ + 1 ] = { 0 } ;
            ossStrncpy( clName, cataInfo->getName(), DMS_COLLECTION_FULL_NAME_SZ ) ;
            if ( SDB_OK == rtnCoordGetRemoteCata( cb, clName, cataInfo ) )
            {
               if ( pHasUpdate )
               {
                  *pHasUpdate = TRUE ;
               }
               if ( NULL == pQuery || pQuery->isEmpty() )
               {
                  cataInfo->getSubCLList( subCLLst ) ;
               }
               else
               {
                  cataInfo->getMatchSubCLs( *pQuery, subCLLst ) ;
               }
            }
         }

         vector< string >::iterator iterCL = subCLLst.begin() ;
         while( iterCL != subCLLst.end() )
         {
            CoordCataInfoPtr subCataInfo ;
            CoordGroupList groupLstTmp ;
            CoordGroupList::iterator iterGrp ;
            rc = rtnCoordGetCataInfo( cb, (*iterCL).c_str(), FALSE,
                                      subCataInfo, NULL ) ;
            PD_RC_CHECK( rc, PDWARNING,
                         "Failed to get sub-collection catalog-info(rc=%d)",
                         rc ) ;
            rc = rtnCoordGetGroupsByCataInfo( subCataInfo,
                                              sendGroupLst,
                                              groupLstTmp,
                                              cb,
                                              NULL,
                                              pQuery ) ;
            PD_RC_CHECK( rc, PDERROR,
                         "Failed to get sub-collection group-info(rc=%d)",
                         rc ) ;

            iterGrp = groupLstTmp.begin() ;
            while ( iterGrp != groupLstTmp.end() )
            {
               groupLst[ iterGrp->first ] = iterGrp->second ;
               ++iterGrp ;
            }
            ++iterCL ;
         }
      }

   done:
      PD_TRACE_EXITRC ( SDB_RTNCOGETGROUPSBYCATAINFO, rc ) ;
      return rc ;
   error:
      goto done ;
   }

   // PD_TRACE_DECLARE_FUNCTION ( SDB_RTNCOSENDREQUESTTONODESWITHOUTREPLY, "rtnCoordSendRequestToNodesWithOutReply" )
   void rtnCoordSendRequestToNodesWithOutReply( void *pBuffer,
                                                ROUTE_SET &nodes,
                                                netMultiRouteAgent *pRouteAgent )
   {
      PD_TRACE_ENTRY ( SDB_RTNCOSENDREQUESTTONODESWITHOUTREPLY ) ;
      pRouteAgent->multiSyncSend( nodes, pBuffer ) ;
      PD_TRACE_EXIT ( SDB_RTNCOSENDREQUESTTONODESWITHOUTREPLY ) ;
   }

   // PD_TRACE_DECLARE_FUNCTION ( SDB_RTNCOSENDREQUESTTONODEWITHOUTCHECK, "rtnCoordSendRequestToNodeWithoutCheck" )
   INT32 rtnCoordSendRequestToNodeWithoutCheck( void *pBuffer,
                                                const MsgRouteID &routeID,
                                                netMultiRouteAgent *pRouteAgent,
                                                pmdEDUCB *cb,
                                                REQUESTID_MAP &sendNodes )
   {
      INT32 rc = SDB_OK;
      UINT64 reqID = 0;
      PD_TRACE_ENTRY ( SDB_RTNCOSENDREQUESTTONODEWITHOUTCHECK ) ;
      rc = pRouteAgent->syncSendWithoutCheck( routeID, pBuffer, reqID, cb );
      PD_RC_CHECK ( rc, PDERROR, "Failed to send the request to node"
                    "(groupID=%u, nodeID=%u, serviceID=%u, rc=%d)",
                    routeID.columns.groupID,
                    routeID.columns.nodeID,
                    routeID.columns.serviceID,
                    rc ) ;
      sendNodes[ reqID ] = routeID ;

   done:
      PD_TRACE_EXITRC ( SDB_RTNCOSENDREQUESTTONODEWITHOUTCHECK, rc ) ;
      return rc;
   error:
      goto done;
   }

   // PD_TRACE_DECLARE_FUNCTION ( SDB_RTNCOSENDREQUESTTONODEWITHOUTREPLY, "rtnCoordSendRequestToNodeWithoutReply" )
   INT32 rtnCoordSendRequestToNodeWithoutReply( void *pBuffer,
                                                MsgRouteID &routeID,
                                                netMultiRouteAgent *pRouteAgent )
   {
      INT32 rc = SDB_OK;
      UINT64 reqID = 0;
      PD_TRACE_ENTRY ( SDB_RTNCOSENDREQUESTTONODEWITHOUTREPLY ) ;
      rc = pRouteAgent->syncSend( routeID, pBuffer, reqID, NULL );
      PD_RC_CHECK ( rc, PDERROR, "Failed to send the request to node"
                    "(groupID=%u, nodeID=%u, serviceID=%u, rc=%d)",
                    routeID.columns.groupID,
                    routeID.columns.nodeID,
                    routeID.columns.serviceID,
                    rc ) ;

   done:
      PD_TRACE_EXITRC ( SDB_RTNCOSENDREQUESTTONODEWITHOUTREPLY, rc ) ;
      return rc;
   error:
      goto done;
   }

   // PD_TRACE_DECLARE_FUNCTION ( SDB_RTNCOSENDREQUESTTONODE, "rtnCoordSendRequestToNode" )
   INT32 rtnCoordSendRequestToNode( void *pBuffer,
                                    MsgRouteID routeID,
                                    netMultiRouteAgent *pRouteAgent,
                                    pmdEDUCB *cb,
                                    REQUESTID_MAP &sendNodes )
   {
      INT32 rc = SDB_OK;
      UINT64 reqID = 0;
      PD_TRACE_ENTRY ( SDB_RTNCOSENDREQUESTTONODE ) ;
      rc = pRouteAgent->syncSend( routeID, pBuffer, reqID, cb );
      PD_RC_CHECK ( rc, PDERROR, "Failed to send the request to node"
                    "(groupID=%u, nodeID=%u, serviceID=%u, rc=%d)",
                    routeID.columns.groupID,
                    routeID.columns.nodeID,
                    routeID.columns.serviceID,
                    rc ) ;
      sendNodes[ reqID ] = routeID ;

   done:
      PD_TRACE_EXITRC ( SDB_RTNCOSENDREQUESTTONODE, rc ) ;
      return rc ;
   error:
      goto done ;
   }

   // PD_TRACE_DECLARE_FUNCTION ( SDB_RTNCOSENDREQUESTTONODE2, "rtnCoordSendRequestToNode" )
   INT32 rtnCoordSendRequestToNode( void *pBuffer,
                                    MsgRouteID routeID,
                                    netMultiRouteAgent *pRouteAgent,
                                    pmdEDUCB *cb,
                                    const netIOVec &iov,
                                    REQUESTID_MAP &sendNodes )
   {
      INT32 rc = SDB_OK;
      UINT64 reqID = 0;
      PD_TRACE_ENTRY ( SDB_RTNCOSENDREQUESTTONODE2 ) ;
      rc = pRouteAgent->syncSend( routeID, ( MsgHeader *)pBuffer, iov, reqID, cb );
      PD_RC_CHECK ( rc, PDERROR,
                  "failed to send the request to node"
                  "(groupID=%u, nodeID=%u, serviceID=%u, rc=%d)",
                  routeID.columns.groupID,
                  routeID.columns.nodeID,
                  routeID.columns.serviceID,
                  rc );
      sendNodes[ reqID ] = routeID;
   done:
      PD_TRACE_EXITRC ( SDB_RTNCOSENDREQUESTTONODE2, rc ) ;
      return rc;
   error:
      goto done;
   }

   // PD_TRACE_DECLARE_FUNCTION ( SDB_RTNCOSENDREQUESTTONODEGROUPS, "rtnCoordSendRequestToNodeGroups" )
   INT32 rtnCoordSendRequestToNodeGroups( CHAR *pBuffer,
                                          CoordGroupList &groupLst,
                                          CoordGroupMap &mapGroupInfo,
                                          BOOLEAN isSendPrimary,
                                          netMultiRouteAgent *pRouteAgent,
                                          pmdEDUCB *cb,
                                          REQUESTID_MAP &sendNodes,
                                          BOOLEAN isResend,
                                          MSG_ROUTE_SERVICE_TYPE type,
                                          BOOLEAN ignoreError )
   {
      INT32 rc = SDB_OK;
      PD_TRACE_ENTRY ( SDB_RTNCOSENDREQUESTTONODEGROUPS ) ;
      CoordGroupList::iterator iter ;

      rc = rtnGroupList2GroupPtr( cb, groupLst, mapGroupInfo, FALSE ) ;
      if( rc )
      {
         PD_LOG( PDERROR, "Get groups info failed, rc: %d", rc ) ;
         goto error ;
      }

      iter = groupLst.begin() ;
      while ( iter != groupLst.end() )
      {
         rc = rtnCoordSendRequestToNodeGroup( pBuffer,
                                              mapGroupInfo[ iter->first ],
                                              isSendPrimary, pRouteAgent,
                                              cb, sendNodes, isResend,
                                              type ) ;
         if ( SDB_OK != rc )
         {
            MsgHeader *pMsg = ( MsgHeader* )pBuffer ;
            PD_LOG( PDERROR, "Send msg[opCode: %d, TID: %u] to group[%u] "
                    "failed, rc:%d", pMsg->opCode, pMsg->TID,
                    iter->first, rc ) ;
            if ( !ignoreError )
            {
               goto error ;
            }
         }
         ++iter;
      }

   done:
      PD_TRACE_EXITRC ( SDB_RTNCOSENDREQUESTTONODEGROUPS, rc ) ;
      return rc ;
   error:
      goto done ;
   }

   // PD_TRACE_DECLARE_FUNCTION ( SDB_RTNCOSENDREQUESTTONODEGROUPS2, "rtnCoordSendRequestToNodeGroups" )
   INT32 rtnCoordSendRequestToNodeGroups( MsgHeader *pBuffer,
                                          CoordGroupList &groupLst,
                                          CoordGroupMap &mapGroupInfo,
                                          BOOLEAN isSendPrimary,
                                          netMultiRouteAgent *pRouteAgent,
                                          pmdEDUCB *cb,
                                          const netIOVec &iov,
                                          REQUESTID_MAP &sendNodes, // out
                                          BOOLEAN isResend,
                                          MSG_ROUTE_SERVICE_TYPE type )
   {
      INT32 rc = SDB_OK ;
      PD_TRACE_ENTRY( SDB_RTNCOSENDREQUESTTONODEGROUPS2 ) ;
      CoordGroupList::iterator iter ;

      rc = rtnGroupList2GroupPtr( cb, groupLst, mapGroupInfo, FALSE ) ;
      if( rc )
      {
         PD_LOG( PDERROR, "Get groups info failed, rc: %d" ) ;
         goto error ;
      }

      iter = groupLst.begin() ;
      while ( iter != groupLst.end() )
      {
         rc = rtnCoordSendRequestToNodeGroup( pBuffer,
                                              mapGroupInfo[ iter->first ],
                                              isSendPrimary, pRouteAgent,
                                              cb, iov, sendNodes,
                                              isResend, type ) ;
         if ( SDB_OK != rc )
         {
            PD_LOG( PDERROR, "Send msg[opCode: %d, TID: %u] to group[%u] "
                    "failed, rc:%d", pBuffer->opCode, pBuffer->TID,
                    iter->first, rc ) ;
            goto error ;
         }
         ++iter ;
      }

   done:
      PD_TRACE_EXITRC( SDB_RTNCOSENDREQUESTTONODEGROUPS2, rc ) ;
      return rc ;
   error:
      goto done ;
   }

   // PD_TRACE_DECLARE_FUNCTION ( SDB_RTNCOSENDREQUESTTONODEGROUPS3, "rtnCoordSendRequestToNodeGroups" )
   INT32 rtnCoordSendRequestToNodeGroups( MsgHeader *pBuffer,
                                          CoordGroupList &groupLst,
                                          CoordGroupMap &mapGroupInfo,
                                          BOOLEAN isSendPrimary,
                                          netMultiRouteAgent *pRouteAgent,
                                          pmdEDUCB *cb,
                                          GROUP_2_IOVEC &iov,
                                          REQUESTID_MAP &sendNodes, // out
                                          BOOLEAN isResend,
                                          MSG_ROUTE_SERVICE_TYPE type )
   {
      INT32 rc = SDB_OK ;
      PD_TRACE_ENTRY( SDB_RTNCOSENDREQUESTTONODEGROUPS3 ) ;
      CoordGroupList::iterator iter ;
      GROUP_2_IOVEC::iterator itIO ;
      netIOVec *pCommonIO = NULL ;
      netIOVec *pIOVec = NULL ;

      rc = rtnGroupList2GroupPtr( cb, groupLst, mapGroupInfo, FALSE ) ;
      if( rc )
      {
         PD_LOG( PDERROR, "Get groups info failed, rc: %d" ) ;
         goto error ;
      }

      itIO = iov.find( 0 ) ; // group id is 0 for common iovec
      if ( iov.end() != itIO )
      {
         pCommonIO = &( itIO->second ) ;
      }

      iter = groupLst.begin() ;
      while ( iter != groupLst.end() )
      {
         itIO = iov.find( iter->first ) ;
         if ( itIO != iov.end() )
         {
            pIOVec = &( itIO->second ) ;
         }
         else if ( pCommonIO )
         {
            pIOVec = pCommonIO ;
         }
         else
         {
            PD_LOG( PDERROR, "Can't find the group[%d]'s iovec datas",
                    iter->first ) ;
            rc = SDB_SYS ;
            SDB_ASSERT( FALSE, "Group iovec is null" ) ;
            goto error ;
         }

         rc = rtnCoordSendRequestToNodeGroup( pBuffer,
                                              mapGroupInfo[ iter->first ],
                                              isSendPrimary, pRouteAgent,
                                              cb, *pIOVec, sendNodes,
                                              isResend, type ) ;
         if ( SDB_OK != rc )
         {
            PD_LOG( PDERROR, "Send msg[opCode: %d, TID: %u] to group[%u] "
                    "failed, rc:%d", pBuffer->opCode, pBuffer->TID,
                    iter->first, rc ) ;
            goto error ;
         }
         ++iter ;
      }

   done:
      PD_TRACE_EXITRC( SDB_RTNCOSENDREQUESTTONODEGROUPS3, rc ) ;
      return rc ;
   error:
      goto done ;
   }

   INT32 rtnCoordSendRequestToNodeGroup( MsgHeader *pBuffer,
                                         CoordGroupInfoPtr &groupInfo,
                                         BOOLEAN isSendPrimary,
                                         netMultiRouteAgent *pRouteAgent,
                                         pmdEDUCB *cb,
                                         const netIOVec &iov,
                                         REQUESTID_MAP &sendNodes,
                                         BOOLEAN isResend,
                                         MSG_ROUTE_SERVICE_TYPE type )
   {
      return _rtnCoordSendRequestToNodeGroup( pBuffer, groupInfo, isSendPrimary,
                                              pRouteAgent, type, cb,
                                              sendNodes, &iov, isResend ) ;
   }

   INT32 rtnCoordSendRequestToNodeGroup( CHAR *pBuffer,
                                         CoordGroupInfoPtr &groupInfo,
                                         BOOLEAN isSendPrimary,
                                         netMultiRouteAgent *pRouteAgent,
                                         pmdEDUCB *cb,
                                         REQUESTID_MAP &sendNodes,
                                         BOOLEAN isResend,
                                         MSG_ROUTE_SERVICE_TYPE type )
   {
      return _rtnCoordSendRequestToNodeGroup( (MsgHeader*)pBuffer, groupInfo,
                                              isSendPrimary, pRouteAgent,
                                              type, cb, sendNodes, NULL,
                                              isResend ) ;
   }

   INT32 rtnCoordSendRequestToPrimary( CHAR *pBuffer,
                                       CoordGroupInfoPtr &groupInfo,
                                       REQUESTID_MAP &sendNodes,
                                       netMultiRouteAgent *pRouteAgent,
                                       MSG_ROUTE_SERVICE_TYPE type,
                                       pmdEDUCB *cb )
   {
      return _rtnCoordSendRequestToPrimary( (MsgHeader*)pBuffer, groupInfo,
                                            pRouteAgent, type, cb,
                                            sendNodes, NULL ) ;
   }

   INT32 rtnCoordSendRequestToPrimary( MsgHeader *pBuffer,
                                       CoordGroupInfoPtr &groupInfo,
                                       REQUESTID_MAP &sendNodes,
                                       netMultiRouteAgent *pRouteAgent,
                                       const netIOVec &iov,
                                       MSG_ROUTE_SERVICE_TYPE type,
                                       pmdEDUCB *cb )
   {
      return _rtnCoordSendRequestToPrimary( pBuffer, groupInfo, pRouteAgent,
                                            type, cb, sendNodes, &iov ) ;
   }

   static void _rtnCoordShufflePositions ( RTN_COORD_POS_ARRAY & positionArray,
                                           RTN_COORD_POS_LIST & positionList )
   {
      for ( UINT32 i = 0 ; i < positionArray.size() ; i ++ )
      {
         UINT32 random = ossRand() % positionArray.size() ;
         if ( i != random )
         {
            UINT8 tmp = positionArray[ i ] ;
            positionArray[ i ] = positionArray[ random ] ;
            positionArray[ random ] = tmp  ;
         }
      }

      RTN_COORD_POS_ARRAY::iterator posIter( positionArray ) ;
      UINT8 tmpPos = 0 ;
      while ( posIter.next( tmpPos ) )
      {
         positionList.push_back( tmpPos ) ;
      }

      positionArray.clear() ;
   }

   static void _rtnCoordSelectPositions ( const VEC_NODE_INFO & groupNodes,
                                          UINT32 primaryPos,
                                          const rtnInstanceOption & instanceOption,
                                          UINT32 random,
                                          RTN_COORD_POS_LIST & selectedPositions )
   {
      RTN_PREFER_INSTANCE_MODE mode = instanceOption.getPreferredMode() ;
      const RTN_INSTANCE_LIST & instanceList = instanceOption.getInstanceList() ;
      RTN_COORD_POS_ARRAY tempPositions ;
      UINT8 unselectMask = 0xFF ;
      UINT32 nodeCount = groupNodes.size() ;
      BOOLEAN foundPrimary = FALSE ;
      BOOLEAN primaryFirst = ( instanceOption.getSpecialInstance() == PREFER_INSTANCE_TYPE_MASTER ) ;
      BOOLEAN primaryLast = ( instanceOption.getSpecialInstance() == PREFER_INSTANCE_TYPE_SLAVE ) ;

      selectedPositions.clear() ;

      if ( nodeCount == 0 || instanceList.empty() )
      {
         goto done ;
      }

      for ( RTN_INSTANCE_LIST::const_iterator instIter = instanceList.begin() ;
            instIter != instanceList.end() ;
            instIter ++ )
      {
         RTN_PREFER_INSTANCE_TYPE instance = (RTN_PREFER_INSTANCE_TYPE)(*instIter) ;
         if ( instance > PREFER_INSTANCE_TYPE_MIN &&
              instance < PREFER_INSTANCE_TYPE_MAX )
         {
            UINT8 pos = 0 ;
            for ( VEC_NODE_INFO::const_iterator nodeIter = groupNodes.begin() ;
                  nodeIter != groupNodes.end() ;
                  nodeIter ++, pos ++ )
            {
               if ( nodeIter->_instanceID == (UINT8)instance )
               {
                  if ( primaryPos == pos )
                  {
                     if ( !primaryFirst && !primaryLast )
                     {
                        tempPositions.append( pos ) ;
                     }
                     foundPrimary = TRUE ;
                  }
                  else
                  {
                     tempPositions.append( pos ) ;
                  }
                  OSS_BIT_CLEAR( unselectMask, 1 << pos ) ;
               }
            }
            if ( !tempPositions.empty() &&
                 PREFER_INSTANCE_MODE_ORDERED == mode )
            {
               _rtnCoordShufflePositions( tempPositions, selectedPositions ) ;
            }
         }
      }

      if ( !tempPositions.empty() )
      {
         _rtnCoordShufflePositions( tempPositions, selectedPositions ) ;
      }

      if ( foundPrimary )
      {
         if ( primaryFirst )
         {
            selectedPositions.push_front( primaryPos ) ;
         }
         else if ( primaryLast )
         {
            selectedPositions.push_back( primaryPos ) ;
         }
      }
      else if ( CLS_RG_NODE_POS_INVALID != primaryPos &&
                !selectedPositions.empty() &&
                ( instanceOption.getSpecialInstance() == PREFER_INSTANCE_TYPE_MASTER ||
                  instanceOption.getSpecialInstance() == PREFER_INSTANCE_TYPE_MASTER_SND ) )
      {
         selectedPositions.push_back( primaryPos ) ;
         OSS_BIT_CLEAR( unselectMask, 1 << primaryPos ) ;
      }

      if ( !selectedPositions.empty() )
      {
         UINT8 tmpPos = (UINT8)random ;
         for ( UINT32 i = 0 ; i < nodeCount ; i ++ )
         {
            tmpPos = ( tmpPos + 1 ) % nodeCount ;
            if ( OSS_BIT_TEST( unselectMask, 1 << tmpPos ) )
            {
               selectedPositions.push_back( (UINT8)tmpPos ) ;
            }
         }
      }

#ifdef _DEBUG
      if ( selectedPositions.empty() )
      {
         PD_LOG( PDDEBUG, "Got no selected node positions" ) ;
      }
      else
      {
         StringBuilder ss ;
         for ( RTN_COORD_POS_LIST::iterator iter = selectedPositions.begin() ;
               iter != selectedPositions.end() ;
               iter ++ )
         {
            if ( iter != selectedPositions.begin() )
            {
               ss << ", " ;
            }
            ss << ( *iter ) ;
         }
         PD_LOG( PDDEBUG, "Got selected node positions : [ %s ]",
                 ss.str().c_str() ) ;
      }
#endif

   done :
      return ;
   }

   // PD_TRACE_DECLARE_FUNCTION ( SDB_RTNCOGETNODEPOS, "rtnCoordGetNodePos" )
   void rtnCoordGetNodePos ( clsGroupItem * pGroupItem,
                             const rtnInstanceOption & instanceOption,
                             UINT32 random,
                             UINT32 & pos,
                             RTN_COORD_POS_LIST & selectedPositions )
   {
      PD_TRACE_ENTRY ( SDB_RTNCOGETNODEPOS ) ;

      BOOLEAN selected = FALSE ;
      UINT32 primaryPos = pGroupItem->getPrimaryPos() ;

      if ( instanceOption.hasCommonInstance() )
      {
         const VEC_NODE_INFO * nodes = pGroupItem->getNodes() ;
         SDB_ASSERT( NULL != nodes, "node list is invalid" ) ;
         _rtnCoordSelectPositions( *nodes, primaryPos, instanceOption, random,
                                   selectedPositions ) ;

         if ( !selectedPositions.empty() )
         {
            pos = selectedPositions.front() ;
            selectedPositions.pop_front() ;
            selected = TRUE ;
         }
      }

      if ( !selected )
      {
         UINT32 nodeCount = pGroupItem->nodeCount() ;
         BOOLEAN isSlavePreferred = FALSE ;
         switch ( instanceOption.getSpecialInstance() )
         {
            case PREFER_INSTANCE_TYPE_MASTER :
            case PREFER_INSTANCE_TYPE_MASTER_SND :
            {
               pos = pGroupItem->getPrimaryPos() ;
               if ( CLS_RG_NODE_POS_INVALID != pos )
               {
                  selected = TRUE ;
               }
               else
               {
                  pos = random ;
               }
               break ;
            }
            case PREFER_INSTANCE_TYPE_SLAVE :
            case PREFER_INSTANCE_TYPE_SLAVE_SND :
            {
               isSlavePreferred = TRUE ;
               pos = random ;
               break ;
            }
            case PREFER_INSTANCE_TYPE_ANYONE :
            case PREFER_INSTANCE_TYPE_ANYONE_SND :
            {
               pos = random ;
               break ;
            }
            default :
            {
               if ( 1 == instanceOption.getInstanceList().size() )
               {
                  pos = instanceOption.getInstanceList().front() - 1 ;
               }
               else
               {
                  pos = random ;
               }
               break ;
            }
         }

         if ( !selected && nodeCount > 0 )
         {
            pos = pos % nodeCount ;
            if ( isSlavePreferred && pos == primaryPos )
            {
               pos = ( pos + 1 ) % nodeCount ;
            }
         }
      }

      PD_TRACE_EXIT( SDB_RTNCOGETNODEPOS ) ;
   }

   // PD_TRACE_DECLARE_FUNCTION ( SDB_RTNCOGETNEXTNODE, "rtnCoordGetNextNode" )
   void rtnCoordGetNextNode ( clsGroupItem *pGroupItem,
                              RTN_COORD_POS_LIST & selectedPositions,
                              const rtnInstanceOption & instanceOption,
                              UINT32 & selTimes,
                              UINT32 & curPos )
   {
      PD_TRACE_ENTRY( SDB_RTNCOGETNEXTNODE ) ;

      BOOLEAN isSlavePreferred = FALSE ;
      UINT32 nodeCount = pGroupItem->nodeCount() ;

      if( selTimes >= nodeCount )
      {
         curPos = CLS_RG_NODE_POS_INVALID ;
      }
      else
      {
         UINT32 tmpPos = curPos ;
         if ( selTimes > 0 )
         {
            if ( selectedPositions.empty() )
            {
               isSlavePreferred = instanceOption.isSlavePerferred() ;
               tmpPos = ( tmpPos + 1 ) % nodeCount ;
            }
            else
            {
               tmpPos = selectedPositions.front() ;
               selectedPositions.pop_front() ;
            }
         }

         if ( isSlavePreferred )
         {
            UINT32 primaryPos = pGroupItem->getPrimaryPos() ;
            if ( CLS_RG_NODE_POS_INVALID != primaryPos &&
                 selTimes + 1 == nodeCount )
            {
               tmpPos = primaryPos ;
            }
            else if ( tmpPos == primaryPos )
            {
               tmpPos = ( tmpPos + 1 ) % nodeCount ;
            }
         }

         curPos = tmpPos ;
         ++selTimes ;
      }

      PD_TRACE_EXIT( SDB_RTNCOGETNEXTNODE ) ;
   }

   INT32 rtnCoordSendRequestToOne( CHAR *pBuffer,
                                   CoordGroupInfoPtr &groupInfo,
                                   REQUESTID_MAP &sendNodes,
                                   netMultiRouteAgent *pRouteAgent,
                                   MSG_ROUTE_SERVICE_TYPE type,
                                   pmdEDUCB *cb,
                                   BOOLEAN isResend )
   {
      return _rtnCoordSendRequestToOne( ( MsgHeader*) pBuffer, groupInfo,
                                         sendNodes, pRouteAgent, type,
                                         cb, NULL, isResend ) ;
   }

   INT32 rtnCoordSendRequestToOne( MsgHeader *pBuffer,
                                   CoordGroupInfoPtr &groupInfo,
                                   REQUESTID_MAP &sendNodes,
                                   netMultiRouteAgent *pRouteAgent,
                                   const netIOVec &iov,
                                   MSG_ROUTE_SERVICE_TYPE type,
                                   pmdEDUCB *cb,
                                   BOOLEAN isResend )
   {
      return _rtnCoordSendRequestToOne( pBuffer, groupInfo, sendNodes,
                                        pRouteAgent, type, cb,
                                        &iov, isResend ) ;
   }

   // PD_TRACE_DECLARE_FUNCTION ( SDB_RTNCOPROCESSGETGROUPREPLY, "rtnCoordProcessGetGroupReply" )
   INT32 rtnCoordProcessGetGroupReply ( MsgHeader *pReply,
                                        CoordGroupInfoPtr &groupInfo )
   {
      INT32 rc = SDB_OK;
      PD_TRACE_ENTRY ( SDB_RTNCOPROCESSGETGROUPREPLY ) ;
      UINT32 headerLen = MSG_GET_INNER_REPLY_HEADER_LEN(pReply) ;

      do
      {
         if ( SDB_OK == MSG_GET_INNER_REPLY_RC(pReply) &&
              (UINT32)pReply->messageLength >= headerLen + 5 )
         {
            try
            {
               BSONObj boGroupInfo( MSG_GET_INNER_REPLY_DATA(pReply) );
               BSONElement beGroupID = boGroupInfo.getField( CAT_GROUPID_NAME );
               if ( beGroupID.eoo() || !beGroupID.isNumber() )
               {
                  rc = SDB_INVALIDARG;
                  PD_LOG ( PDERROR,
                           "Process get-group-info-reply failed,"
                           "failed to get the field(%s)", CAT_GROUPID_NAME );
                  break;
               }
               CoordGroupInfo *pGroupInfo
                           = SDB_OSS_NEW CoordGroupInfo( beGroupID.number() );
               if ( NULL == pGroupInfo )
               {
                  rc = SDB_OOM;
                  PD_LOG ( PDERROR, "Process get-group-info-reply failed,"
                           "new failed ");
                  break;
               }
               CoordGroupInfoPtr groupInfoTmp( pGroupInfo );
               rc = groupInfoTmp->updateGroupItem( boGroupInfo );
               if ( rc != SDB_OK )
               {
                  PD_LOG ( PDERROR, "Process get-group-info-reply failed,"
                           "failed to parse the groupInfo(rc=%d)", rc );
                  break;
               }
               groupInfo = groupInfoTmp;
            }
            catch ( std::exception &e )
            {
               rc = SDB_INVALIDARG;
               PD_LOG ( PDERROR, "Process get-group-info-reply failed,"
                        "received unexpected error:%s", e.what() );
               break;
            }
         }
         else
         {
            rc = MSG_GET_INNER_REPLY_RC(pReply) ;
         }
      }while ( FALSE );
      PD_TRACE_EXITRC ( SDB_RTNCOPROCESSGETGROUPREPLY, rc ) ;
      return rc;
   }

   // PD_TRACE_DECLARE_FUNCTION ( SDB_RTNCOPROCESSQUERYCATREPLY, "rtnCoordProcessQueryCatReply" )
   INT32 rtnCoordProcessQueryCatReply ( MsgCatQueryCatRsp *pReply,
                                        const CHAR *pCollectionName,
                                        CoordCataInfoPtr &cataInfo )
   {
      INT32 rc = SDB_OK ;

      PD_TRACE_ENTRY ( SDB_RTNCOPROCESSQUERYCATREPLY ) ;

      PD_CHECK ( SDB_OK == pReply->flags,
                 pReply->flags, error, PDWARNING,
                 "Received unexpected reply while query "
                 "catalogue(flag=%d)", pReply->flags ) ;

      try
      {
         pmdKRCB *pKrcb           = pmdGetKRCB() ;
         CoordCB *pCoordcb        = pKrcb->getCoordCB() ;
         INT32 flag               = 0 ;
         INT64 contextID          = -1 ;
         INT32 startFrom          = 0 ;
         INT32 numReturned        = 0 ;
         vector < BSONObj > objList ;

         rc = msgExtractReply ( (CHAR *)pReply, &flag, &contextID, &startFrom,
                                &numReturned, objList ) ;
         PD_RC_CHECK( rc, PDERROR,
                      "Failed to extract reply msg, rc = %d", rc ) ;

         for ( UINT32 i = 0 ; i < objList.size() ; i ++ )
         {
            CoordCataInfoPtr tmpInfo ;

            rc = rtnCoordProcessCatInfoObj( objList[i], tmpInfo ) ;
            PD_RC_CHECK( rc, PDERROR,
                         "Failed to parse query-catalogue-reply, "
                         "parse catalogue info from bson-obj failed, rc: %d",
                         rc ) ;
            pCoordcb->updateCataInfo( tmpInfo->getName(), tmpInfo ) ;

            if ( 0 == ossStrcmp( tmpInfo->getName(), pCollectionName ) )
            {
               cataInfo = tmpInfo ;
            }
         }

         if ( !cataInfo.get() )
         {
            rc = SDB_DMS_NOTEXIST ;
            PD_RC_CHECK( rc, PDERROR,
                         "Failed to get required collection [%s] from "
                         "query-catalogue-reply", pCollectionName ) ;
         }
      }
      catch ( std::exception &e )
      {
         rc = SDB_INVALIDARG ;
         PD_LOG ( PDERROR, "Failed to parse query-catalogue-reply,"
                  "received unexpected error:%s", e.what() ) ;
         goto error ;
      }

      PD_TRACE_EXITRC ( SDB_RTNCOPROCESSQUERYCATREPLY, rc ) ;

   done :
      return rc ;
   error :
      goto done ;
   }

   // PD_TRACE_DECLARE_FUNCTION ( SDB_RTNCOPROCESSCATINFOOBJ, "rtnCoordProcessCatInfoObj" )
   INT32 rtnCoordProcessCatInfoObj ( const BSONObj &boCataInfo,
                                     CoordCataInfoPtr &cataInfo )
   {
      INT32 rc = SDB_OK ;

      PD_TRACE_ENTRY( SDB_RTNCOPROCESSCATINFOOBJ ) ;

      try
      {
         BSONElement beName = boCataInfo.getField( CAT_CATALOGNAME_NAME ) ;
         PD_CHECK( !beName.eoo() && beName.type() == String,
                   SDB_INVALIDARG, error, PDERROR,
                   "Failed to parse query-catalogue-reply,"
                   "failed to get the field [%s]",
                   CAT_CATALOGNAME_NAME ) ;
         BSONElement beVersion = boCataInfo.getField( CAT_CATALOGVERSION_NAME ) ;
         PD_CHECK( !beVersion.eoo() && beVersion.isNumber(),
                   SDB_INVALIDARG, error, PDERROR,
                   "Failed to parse query-catalogue-reply, "
                   "failed to get the field [%s]",
                   CAT_CATALOGVERSION_NAME ) ;

         CoordCataInfo *pCataInfoTmp = NULL ;
         pCataInfoTmp = SDB_OSS_NEW CoordCataInfo( beVersion.number(),
                                                   beName.str().c_str() ) ;
         PD_CHECK( pCataInfoTmp,
                   SDB_OOM, error, PDERROR,
                   "Failed to parse query-catalogue-reply, new failed" ) ;

         CoordCataInfoPtr cataInfoPtr( pCataInfoTmp ) ;
         rc = cataInfoPtr->fromBSONObj( boCataInfo ) ;
         PD_RC_CHECK( rc, PDERROR,
                      "Failed to parse query-catalogue-reply, "
                      "parse catalogue info from bson-obj failed, rc: %d",
                      rc ) ;

         PD_LOG ( PDDEBUG, "new catalog info: %s",
                  boCataInfo.toString().c_str() ) ;
         cataInfo = cataInfoPtr ;
      }
      catch ( std::exception &e )
      {
         rc = SDB_INVALIDARG ;
         PD_LOG ( PDERROR, "Failed to parse query-catalogue-reply,"
                  "received unexpected error: %s", e.what() ) ;
         goto error ;
      }

   done :
      PD_TRACE_EXITRC( SDB_RTNCOPROCESSCATINFOOBJ, rc ) ;
      return rc ;
   error :
      goto done ;
   }

   INT32 rtnCoordGetAllGroupList( pmdEDUCB * cb, CoordGroupList &groupList,
                                  const BSONObj *query, BOOLEAN exceptCata,
                                  BOOLEAN exceptCoord,
                                  BOOLEAN useLocalWhenFailed )
   {
      INT32 rc = SDB_OK ;
      GROUP_VEC vecGrpPtr ;

      rc = rtnCoordGetAllGroupList( cb, vecGrpPtr, query, exceptCata,
                                    exceptCoord, useLocalWhenFailed ) ;
      if ( rc )
      {
         goto error ;
      }
      else
      {
         GROUP_VEC::iterator it = vecGrpPtr.begin() ;
         while ( it != vecGrpPtr.end() )
         {
            CoordGroupInfoPtr &ptr = *it ;
            groupList[ ptr->groupID() ] = ptr->groupID() ;
            ++it ;
         }
      }

   done:
      return rc ;
   error:
      goto done ;
   }

   INT32 rtnCoordGetAllGroupList( pmdEDUCB * cb, GROUP_VEC &groupLst,
                                  const BSONObj *query, BOOLEAN exceptCata,
                                  BOOLEAN exceptCoord,
                                  BOOLEAN useLocalWhenFailed )
   {
      INT32 rc = SDB_OK;
      pmdKRCB *pKrcb = pmdGetKRCB();
      SDB_RTNCB *pRtncb = pKrcb->getRTNCB();
      CoordCB *pCoordcb = pKrcb->getCoordCB();
      INT32 bufferSize = 0;
      rtnCoordCommand *pCmdProcesser = NULL;
      CHAR *pListReq = NULL;
      SINT64 contextID = -1;
      rtnContextBuf buffObj ;
      UINT64 identify = pCoordcb->getGrpIdentify() ;

      rtnCoordProcesserFactory *pProcesserFactory
               = pCoordcb->getProcesserFactory();
      pCmdProcesser = pProcesserFactory->getCommandProcesser(
         COORD_CMD_LISTGROUPS ) ;
      SDB_ASSERT( pCmdProcesser, "pCmdProcesser can't be NULL!" );
      rc = msgBuildQueryMsg( &pListReq, &bufferSize, COORD_CMD_LISTGROUPS,
                             0, 0, 0, -1, query ) ;
      PD_RC_CHECK( rc, PDERROR, "failed to build list groups request(rc=%d)",
                   rc );
      rc = pCmdProcesser->execute( (MsgHeader*)pListReq, cb,
                                   contextID, &buffObj ) ;
      PD_RC_CHECK( rc, PDERROR, "failed to list groups(rc=%d)", rc ) ;

      while ( TRUE )
      {
         rc = rtnGetMore( contextID, 1, buffObj, cb, pRtncb ) ;
         if ( rc )
         {
            if ( rc != SDB_DMS_EOC )
            {
               PD_RC_CHECK( rc, PDERROR, "failed to execute getmore(rc=%d)",
                            rc );
            }
            contextID = -1 ;
            rc = SDB_OK ;
            break ;
         }

         try
         {
            CoordGroupInfo *pGroupInfo = NULL ;
            CoordGroupInfoPtr groupInfoTmp ;
            BSONObj boGroupInfo( buffObj.data() ) ;
            BSONElement beGroupID = boGroupInfo.getField( CAT_GROUPID_NAME );
            PD_CHECK( beGroupID.isNumber(), SDB_INVALIDARG, error, PDERROR,
                      "failed to process group info, failed to get the field"
                      "(%s)", CAT_GROUPID_NAME ) ;
            pGroupInfo = SDB_OSS_NEW CoordGroupInfo( beGroupID.number() );
            PD_CHECK( pGroupInfo != NULL, SDB_OOM, error, PDERROR,
                      "malloc failed!" );
            groupInfoTmp = CoordGroupInfoPtr( pGroupInfo ) ;
            rc = groupInfoTmp->updateGroupItem( boGroupInfo ) ;
            PD_RC_CHECK( rc, PDERROR, "failed to parse the group info(rc=%d)",
                         rc ) ;

            if ( groupInfoTmp->groupID() == CATALOG_GROUPID )
            {
               if ( !exceptCata )
               {
                  groupLst.push_back( groupInfoTmp ) ;
               }
            }
            else if ( groupInfoTmp->groupID() == COORD_GROUPID )
            {
               if ( !exceptCoord )
               {
                  groupLst.push_back( groupInfoTmp ) ;
               }
            }
            else
            {
               groupLst.push_back( groupInfoTmp );
            }
            pCoordcb->addGroupInfo( groupInfoTmp );
            rc = rtnCoordUpdateRoute( groupInfoTmp, pCoordcb->getRouteAgent(),
                                      MSG_ROUTE_SHARD_SERVCIE ) ;
            if ( groupInfoTmp->groupID() == CATALOG_GROUPID )
            {
               rtnCoordUpdateRoute( groupInfoTmp, pCoordcb->getRouteAgent(),
                                    MSG_ROUTE_CAT_SERVICE ) ;
               pCoordcb->updateCatGroupInfo( groupInfoTmp ) ;
            }
         }
         catch ( std::exception &e )
         {
            rc = SDB_SYS ;
            PD_RC_CHECK( rc, PDERROR, "Failed to process group info, received "
                         "unexpected error:%s", e.what() ) ;
         }
      }

      if ( NULL == query || query->isEmpty() )
      {
         pCoordcb->invalidateGroupInfo( identify ) ;
      }

   done:
      if ( -1 != contextID )
      {
         pRtncb->contextDelete( contextID, cb );
      }
      SAFE_OSS_FREE( pListReq ) ;
      return rc;
   error:
      if ( useLocalWhenFailed )
      {
         pCoordcb->getLocalGroupList( groupLst, exceptCata, exceptCoord ) ;
         rc = SDB_OK ;
      }
      goto done;
   }

   INT32 rtnCoordSendRequestToNodes( void *pBuffer,
                                     ROUTE_SET &nodes,
                                     netMultiRouteAgent *pRouteAgent,
                                     pmdEDUCB *cb,
                                     REQUESTID_MAP &sendNodes,
                                     ROUTE_RC_MAP &failedNodes )
   {
      INT32 rc = SDB_OK;
      ROUTE_SET::iterator iter = nodes.begin();
      while( iter != nodes.end() )
      {
         MsgRouteID routeID;
         routeID.value = *iter;
         rc = rtnCoordSendRequestToNode( pBuffer, routeID,
                                         pRouteAgent, cb,
                                         sendNodes );
         if ( rc != SDB_OK )
         {
            failedNodes[ *iter ] = rc ;
         }
         ++iter;
      }
      return SDB_OK ;
   }

   INT32 rtnCoordReadALine( const CHAR *&pInput,
                            CHAR *pOutput )
   {
      INT32 rc = SDB_OK;
      while( *pInput != 0x0D && *pInput != 0x0A && *pInput != '\0' )
      {
         *pOutput = *pInput;
         ++pInput;
         ++pOutput;
      }
      *pOutput = '\0';
      return rc;
   }

   void rtnCoordClearRequest( pmdEDUCB *cb, REQUESTID_MAP &sendNodes,
                              BOOLEAN interrupt )
   {
      ROUTE_SET nodes ;
      REQUESTID_MAP::iterator iterMap = sendNodes.begin();
      while( iterMap != sendNodes.end() )
      {
         nodes.insert( iterMap->second.value ) ;
         cb->getCoordSession()->delRequest( iterMap->first );
         iterMap = sendNodes.erase( iterMap );
      }
      if ( interrupt && !nodes.empty() )
      {
         netMultiRouteAgent * pRouteAgent =
                                pmdGetKRCB()->getCoordCB()->getRouteAgent() ;

         MsgHeader interruptMsg ;
         interruptMsg.messageLength = sizeof( MsgHeader ) ;
         interruptMsg.opCode = MSG_BS_INTERRUPTE_SELF ;
         interruptMsg.TID = cb->getTID() ;
         interruptMsg.routeID.value = MSG_INVALID_ROUTEID ;

         rtnCoordSendRequestToNodesWithOutReply( (void *)(&interruptMsg),
                                                 nodes,
                                                 pRouteAgent ) ;
      }
   }

   INT32 rtnCoordGetSubCLsByGroups( const CoordSubCLlist &subCLList,
                                    const CoordGroupList &sendGroupList,
                                    pmdEDUCB *cb,
                                    CoordGroupSubCLMap &groupSubCLMap,
                                    const BSONObj *query )
   {
      INT32 rc = SDB_OK;
      CoordGroupList::const_iterator iterSend;
      CoordSubCLlist::const_iterator iterCL = subCLList.begin();
      while( iterCL != subCLList.end() )
      {
         CoordCataInfoPtr cataInfo;
         CoordGroupList groupList;
         CoordGroupList::iterator iterGroup;
         rc = rtnCoordGetCataInfo( cb, (*iterCL).c_str(),
                                   FALSE, cataInfo );
         PD_RC_CHECK( rc, PDWARNING, "Failed to get catalog info of "
                      "sub-collection[%s], rc: %d", (*iterCL).c_str(),
                      rc ) ;
         if ( NULL == query || query->isEmpty() )
         {
            cataInfo->getGroupLst( groupList );
         }
         else
         {
            cataInfo->getGroupByMatcher( *query, groupList ) ;
         }
         iterGroup = groupList.begin();
         while( iterGroup != groupList.end() )
         {
            groupSubCLMap[ iterGroup->first ].push_back( (*iterCL) );
            ++iterGroup;
         }
         ++iterCL;
      }

      iterSend = sendGroupList.begin();
      while( iterSend != sendGroupList.end() )
      {
         groupSubCLMap.erase( iterSend->first ) ;
         ++iterSend;
      }

   done:
      return rc;
   error:
      goto done;
   }

   static INT32 _rtnCoordParseGroupsInfo( const BSONObj &obj,
                                          vector< INT32 > &vecID,
                                          vector< const CHAR* > &vecName,
                                          BSONObj *pNewObj )
   {
      INT32 rc = SDB_OK ;
      BSONObjBuilder builder ;
      BOOLEAN isModify = FALSE ;

      BSONObjIterator it( obj ) ;
      while( it.more() )
      {
         BSONElement ele = it.next() ;

         if ( Array == ele.type() &&
              0 == ossStrcmp( ele.fieldName(), "$and" ) )
         {
            BSONArrayBuilder sub( builder.subarrayStart( ele.fieldName() ) ) ;
            BSONObj tmpNew ;
            BSONObjIterator tmpItr( ele.embeddedObject() ) ;
            while ( tmpItr.more() )
            {
               BSONElement tmpE = tmpItr.next() ;
               if ( Object != tmpE.type() )
               {
                  PD_LOG( PDERROR, "Parse obj[%s] groups failed: "
                          "invalid $and", obj.toString().c_str() ) ;
                  rc = SDB_INVALIDARG ;
                  goto error ;
               }
               else
               {
                  BSONObj tmpObj = tmpE.embeddedObject() ;
                  rc = _rtnCoordParseGroupsInfo( tmpObj, vecID, vecName,
                                                 pNewObj ? &tmpNew : NULL ) ;
                  PD_RC_CHECK( rc, PDERROR, "Parse obj[%s] groups failed",
                               obj.toString().c_str() ) ;
                  if ( pNewObj )
                  {
                     if ( tmpNew.objdata() != tmpObj.objdata() )
                     {
                        isModify = TRUE ;
                        sub.append( tmpNew ) ;
                     }
                     else
                     {
                        sub.append( tmpObj ) ;
                     }
                  }
               }
            }
            sub.done() ;
         } /// end $and
         else if ( 0 == ossStrcasecmp( ele.fieldName(), CAT_GROUPID_NAME ) &&
                   SDB_OK == _rtnCoordParseInt( ele, vecID,
                                                RTN_COORD_PARSE_MASK_ALL ) )
         {
            isModify = TRUE ;
         }
         else if ( ( 0 == ossStrcasecmp( ele.fieldName(),
                                       FIELD_NAME_GROUPNAME ) ||
                     0 == ossStrcasecmp( ele.fieldName(),
                                       FIELD_NAME_GROUPS ) ) &&
                    SDB_OK == _rtnCoordParseString( ele, vecName,
                                                    RTN_COORD_PARSE_MASK_ALL ) )
         {
            isModify = TRUE ;
         }
         else if ( pNewObj )
         {
            builder.append( ele ) ;
         }
      }

      if ( pNewObj )
      {
         if ( isModify )
         {
            *pNewObj = builder.obj() ;
         }
         else
         {
            *pNewObj = obj ;
         }
      }

   done:
      return rc ;
   error:
      goto done ;
   }

   INT32 rtnCoordParseGroupList( pmdEDUCB *cb,
                                 const BSONObj &obj,
                                 CoordGroupList &groupList,
                                 BSONObj *pNewObj )
   {
      INT32 rc = SDB_OK ;
      CoordGroupInfoPtr grpPtr ;
      vector< INT32 > tmpVecInt ;
      vector< const CHAR* > tmpVecStr ;
      UINT32 i = 0 ;

      rc = _rtnCoordParseGroupsInfo( obj, tmpVecInt, tmpVecStr, pNewObj ) ;
      PD_RC_CHECK( rc, PDERROR, "Parse object[%s] group list failed, rc: %d",
                   obj.toString().c_str(), rc ) ;

      for ( i = 0 ; i < tmpVecInt.size() ; ++i )
      {
         groupList[(UINT32)tmpVecInt[i]] = (UINT32)tmpVecInt[i] ;
      }

      for ( i = 0 ; i < tmpVecStr.size() ; ++i )
      {
         rc = rtnCoordGetGroupInfo( cb, tmpVecStr[i], FALSE, grpPtr ) ;
         PD_RC_CHECK( rc, PDERROR, "Get group[%s] info failed, rc: %d",
                      tmpVecStr[i], rc ) ;
         groupList[ grpPtr->groupID() ] = grpPtr->groupID() ;
      }

   done:
      return rc ;
   error:
      goto done ;
   }

   BSONObj* rtnCoordGetFilterByID( FILTER_BSON_ID filterID,
                                   rtnQueryOptions &queryOption )
   {
      BSONObj *pFilter = NULL ;
      switch ( filterID )
      {
         case FILTER_ID_SELECTOR:
            pFilter = &queryOption._selector ;
            break ;
         case FILTER_ID_ORDERBY:
            pFilter = &queryOption._orderBy ;
            break ;
         case FILTER_ID_HINT:
            pFilter = &queryOption._hint ;
            break ;
         default:
            pFilter = &queryOption._query ;
            break ;
      }
      return pFilter ;
   }

   INT32 rtnCoordParseGroupList( pmdEDUCB *cb, MsgOpQuery *pMsg,
                                 FILTER_BSON_ID filterObjID,
                                 CoordGroupList &groupList )
   {
      INT32 rc = SDB_OK ;
      rtnQueryOptions queryOption ;
      BSONObj *pFilterObj = NULL ;

      rc = queryOption.fromQueryMsg( (CHAR *)pMsg ) ;
      PD_RC_CHECK( rc, PDERROR, "Extract query msg failed, rc: %d", rc ) ;

      pFilterObj = rtnCoordGetFilterByID( filterObjID, queryOption ) ;
      try
      {
         if ( !pFilterObj->isEmpty() )
         {
            rc = rtnCoordParseGroupList( cb, *pFilterObj, groupList ) ;
            if ( rc )
            {
               goto error ;
            }
         }
      }
      catch ( std::exception &e )
      {
         PD_LOG( PDERROR, "Occur exception: %s", e.what() ) ;
         rc = SDB_SYS ;
         goto error ;
      }

   done:
      return rc ;
   error:
      goto done ;
   }

   INT32 rtnGroupList2GroupPtr( pmdEDUCB *cb, CoordGroupList &groupList,
                                CoordGroupMap &groupMap,
                                BOOLEAN reNew )
   {
      INT32 rc = SDB_OK ;
      CoordGroupInfoPtr ptr ;

      if ( reNew )
      {
         groupMap.clear() ;
      }

      CoordGroupList::iterator it = groupList.begin() ;
      while ( it != groupList.end() )
      {
         if ( !reNew && groupMap.end() != groupMap.find( it->second ) )
         {
            ++it ;
            continue ;
         }
         rc = rtnCoordGetGroupInfo( cb, it->second, FALSE, ptr ) ;
         PD_RC_CHECK( rc, PDERROR, "Failed to get group[%d] info, rc: %d",
                      it->second, rc ) ;
         groupMap[ it->second ] = ptr ;
         ++it ;
      }

   done:
      return rc ;
   error:
      goto done ;
   }

   INT32 rtnGroupList2GroupPtr( pmdEDUCB * cb, CoordGroupList & groupList,
                                GROUP_VEC & groupPtrs )
   {
      INT32 rc = SDB_OK ;
      groupPtrs.clear() ;
      CoordGroupInfoPtr ptr ;

      CoordGroupList::iterator it = groupList.begin() ;
      while ( it != groupList.end() )
      {
         rc = rtnCoordGetGroupInfo( cb, it->second, FALSE, ptr ) ;
         PD_RC_CHECK( rc, PDERROR, "Failed to get group[%d] info, rc: %d",
                      it->second, rc ) ;
         groupPtrs.push_back( ptr ) ;
         ++it ;
      }

   done:
      return rc ;
   error:
      goto done ;
   }

   INT32 rtnGroupPtr2GroupList( pmdEDUCB * cb, GROUP_VEC & groupPtrs,
                                CoordGroupList & groupList )
   {
      groupList.clear() ;

      GROUP_VEC::iterator it = groupPtrs.begin() ;
      while ( it != groupPtrs.end() )
      {
         CoordGroupInfoPtr &ptr = *it ;
         groupList[ ptr->groupID() ] = ptr->groupID() ;
         ++it ;
      }

      return SDB_OK ;
   }

   static INT32 _rtnCoordParseNodesInfo( const BSONObj &obj,
                                         vector< INT32 > &vecNodeID,
                                         vector< const CHAR* > &vecHostName,
                                         vector< const CHAR* > &vecSvcName,
                                         BSONObj *pNewObj )
   {
      INT32 rc = SDB_OK ;
      BSONObjBuilder builder ;
      BOOLEAN isModify = FALSE ;

      BSONObjIterator itr( obj ) ;
      while( itr.more() )
      {
         BSONElement ele = itr.next() ;

         if ( Array == ele.type() &&
              0 == ossStrcmp( ele.fieldName(), "$and" ) )
         {
            BSONArrayBuilder sub( builder.subarrayStart( ele.fieldName() ) ) ;
            BSONObj tmpNew ;
            BSONObjIterator tmpItr( ele.embeddedObject() ) ;
            while ( tmpItr.more() )
            {
               BSONElement tmpE = tmpItr.next() ;
               if ( Object != tmpE.type() )
               {
                  PD_LOG( PDERROR, "Parse obj[%s] nodes failed: "
                          "invalid $and", obj.toString().c_str() ) ;
                  rc = SDB_INVALIDARG ;
                  goto error ;
               }
               else
               {
                  BSONObj tmpObj = tmpE.embeddedObject() ;
                  rc = _rtnCoordParseNodesInfo( tmpObj, vecNodeID,
                                                vecHostName, vecSvcName,
                                                pNewObj ? &tmpNew : NULL ) ;
                  PD_RC_CHECK( rc, PDERROR, "Parse obj[%s] nodes failed ",
                               obj.toString().c_str() ) ;
                  if ( pNewObj )
                  {
                     if ( tmpNew.objdata() != tmpObj.objdata() )
                     {
                        isModify = TRUE ;
                        sub.append( tmpNew ) ;
                     }
                     else
                     {
                        sub.append( tmpObj ) ;
                     }
                  }
               }
            }
            sub.done() ;
         } /// end $and
         else if ( 0 == ossStrcasecmp( ele.fieldName(), CAT_NODEID_NAME ) &&
                   SDB_OK == _rtnCoordParseInt( ele, vecNodeID,
                                                RTN_COORD_PARSE_MASK_ALL ) )
         {
            isModify = TRUE ;
         }
         else if ( 0 == ossStrcasecmp( ele.fieldName(), FIELD_NAME_HOST ) &&
                   SDB_OK == _rtnCoordParseString( ele, vecHostName,
                                                   RTN_COORD_PARSE_MASK_ALL ) )
         {
            isModify = TRUE ;
         }
         else if ( ( 0 == ossStrcasecmp( ele.fieldName(),
                                         FIELD_NAME_SERVICE_NAME ) ||
                     0 == ossStrcasecmp( ele.fieldName(),
                                         PMD_OPTION_SVCNAME ) ) &&
                    SDB_OK == _rtnCoordParseString( ele, vecSvcName,
                                                    RTN_COORD_PARSE_MASK_ALL ) )
         {
            isModify = TRUE ;
         }
         else if ( pNewObj )
         {
            builder.append( ele ) ;
         } // end if
      } // end while

      if ( pNewObj )
      {
         if ( isModify )
         {
            *pNewObj = builder.obj() ;
         }
         else
         {
            *pNewObj = obj ;
         }
      }

   done:
      return rc ;
   error:
      goto done ;
   }

   INT32 rtnCoordGetGroupNodes( pmdEDUCB *cb, const BSONObj &filterObj,
                                NODE_SEL_STY emptyFilterSel,
                                CoordGroupList &groupList, ROUTE_SET &nodes,
                                BSONObj *pNewObj )
   {
      INT32 rc = SDB_OK ;
      CoordGroupInfoPtr ptr ;
      MsgRouteID routeID ;
      GROUP_VEC groupPtrs ;
      GROUP_VEC::iterator it ;

      vector< INT32 > vecNodeID ;
      vector< const CHAR* > vecHostName ;
      vector< const CHAR* > vecSvcName ;
      BOOLEAN emptyFilter = TRUE ;

      nodes.clear() ;

      rc = rtnGroupList2GroupPtr( cb, groupList, groupPtrs ) ;
      PD_RC_CHECK( rc, PDERROR, "Group ids to group info failed, rc: %d", rc ) ;

      rc = _rtnCoordParseNodesInfo( filterObj, vecNodeID, vecHostName,
                                    vecSvcName, pNewObj ) ;
      PD_RC_CHECK( rc, PDERROR, "Parse obj[%s] nodes info failed, rc: %d",
                   filterObj.toString().c_str(), rc ) ;

      if ( vecNodeID.size() > 0 || vecHostName.size() > 0 ||
           vecSvcName.size() > 0 )
      {
         emptyFilter = FALSE ;
      }

      it = groupPtrs.begin() ;
      while ( it != groupPtrs.end() )
      {
         UINT32 calTimes = 0 ;
         UINT32 randNum = ossRand() ;
         ptr = *it ;

         routeID.value = MSG_INVALID_ROUTEID ;
         clsGroupItem *grp = ptr.get() ;
         if ( grp->nodeCount() > 0 )
         {
            randNum %= grp->nodeCount() ;
         }

         while ( calTimes++ < grp->nodeCount() )
         {
            if ( NODE_SEL_SECONDARY == emptyFilterSel &&
                 randNum == grp->getPrimaryPos() )
            {
               randNum = ( randNum + 1 ) % grp->nodeCount() ;
               continue ;
            }
            else if ( NODE_SEL_PRIMARY == emptyFilterSel &&
                      CLS_RG_NODE_POS_INVALID != grp->getPrimaryPos() )
            {
               randNum = grp->getPrimaryPos() ;
            }
            break ;
         }

         routeID.columns.groupID = grp->groupID() ;
         const VEC_NODE_INFO *nodesInfo = grp->getNodes() ;
         for ( VEC_NODE_INFO::const_iterator itrn = nodesInfo->begin() ;
               itrn != nodesInfo->end();
               ++itrn, --randNum )
         {
            if ( FALSE == emptyFilter )
            {
               BOOLEAN findNode = FALSE ;
               UINT32 index = 0 ;
               for ( index = 0 ; index < vecNodeID.size() ; ++index )
               {
                  if ( (UINT16)vecNodeID[ index ] == itrn->_id.columns.nodeID )
                  {
                     findNode = TRUE ;
                     break ;
                  }
               }
               if ( index > 0 && !findNode )
               {
                  continue ;
               }

               findNode = FALSE ;
               for ( index = 0 ; index < vecHostName.size() ; ++index )
               {
                  if ( 0 == ossStrcmp( vecHostName[ index ],
                                       itrn->_host ) )
                  {
                     findNode = TRUE ;
                     break ;
                  }
               }
               if ( index > 0 && !findNode )
               {
                  continue ;
               }

               findNode = FALSE ;
               for ( index = 0 ; index < vecSvcName.size() ; ++index )
               {
                  if ( 0 == ossStrcmp( vecSvcName[ index ],
                                       itrn->_service[MSG_ROUTE_LOCAL_SERVICE].c_str() ) )
                  {
                     findNode = TRUE ;
                     break ;
                  }
               }
               if ( index > 0 && !findNode )
               {
                  continue ;
               }
            }
            else if ( NODE_SEL_ALL != emptyFilterSel && 0 != randNum )
            {
               continue ;
            }
            routeID.columns.nodeID = itrn->_id.columns.nodeID ;
            routeID.columns.serviceID = MSG_ROUTE_SHARD_SERVCIE ;
            nodes.insert( routeID.value ) ;
         }
         ++it ;
      }

   done:
      return rc ;
   error:
      goto done ;
   }

   // PD_TRACE_DECLARE_FUNCTION ( SDB_RTNCOUPNODESTATBYRC, "rtnCoordUpdateNodeStatByRC" )
   void rtnCoordUpdateNodeStatByRC( pmdEDUCB *cb,
                                    const MsgRouteID &routeID,
                                    CoordGroupInfoPtr &groupInfo,
                                    INT32 retCode )
   {
      PD_TRACE_ENTRY ( SDB_RTNCOUPNODESTATBYRC ) ;

      CoordSession *pSession = cb->getCoordSession() ;
      if ( MSG_INVALID_ROUTEID == routeID.value )
      {
         goto done;
      }
      else if ( SDB_OK != retCode && pSession )
      {
         pSession->removeLastNode( routeID.columns.groupID,
                                   routeID ) ;
      }

      if( groupInfo.get() )
      {
         groupInfo->updateNodeStat( routeID.columns.nodeID,
                                    netResult2Status( retCode ) ) ;
      }

   done:
      PD_TRACE_EXIT ( SDB_RTNCOUPNODESTATBYRC ) ;
      return ;
   }

   BOOLEAN rtnCoordGroupReplyCheck( pmdEDUCB *cb, INT32 flag,
                                    BOOLEAN canRetry,
                                    const MsgRouteID &nodeID,
                                    CoordGroupInfoPtr &groupInfo,
                                    BOOLEAN *pUpdate,
                                    BOOLEAN canUpdate,
                                    UINT32 primaryID,
                                    BOOLEAN isReadCmd )
   {
      if ( cb && cb->getCoordSession() )
      {
         cb->getCoordSession()->removeLastNode( nodeID.columns.groupID,
                                                nodeID ) ;
      }

      if ( SDB_CLS_NOT_PRIMARY == flag && 0 != primaryID &&
           groupInfo.get() )
      {
         INT32 preStat = NET_NODE_STAT_NORMAL ;
         MsgRouteID primaryNodeID ;
         primaryNodeID.value = nodeID.value ;
         primaryNodeID.columns.nodeID = primaryID ;
         if ( SDB_OK == groupInfo->updatePrimary( primaryNodeID,
                                                  TRUE, &preStat ) )
         {
            if ( NET_NODE_STAT_NORMAL != preStat )
            {
               groupInfo->cancelPrimary() ;
               PD_LOG( PDWARNING, "Primary node[%d.%d] is crashed, sleep "
                       "%d seconds", primaryNodeID.columns.groupID,
                       primaryNodeID.columns.nodeID,
                       NET_NODE_FAULTUP_MIN_TIME ) ;
               ossSleep( NET_NODE_FAULTUP_MIN_TIME * OSS_ONE_SEC ) ;
            }
            return canRetry ;
         }
      }

      if ( !canRetry )
      {
         return FALSE ;
      }

      if ( SDB_CLS_NOT_PRIMARY == flag )
      {
         if ( groupInfo.get() )
         {
            groupInfo->updatePrimary( nodeID, FALSE ) ;
         }

         if ( canUpdate )
         {
            UINT32 groupID = nodeID.columns.groupID ;
            INT32 rc = rtnCoordGetRemoteGroupInfo( cb, groupID, NULL,
                                                   groupInfo, TRUE ) ;
            if ( SDB_OK == rc )
            {
               if( pUpdate )
               {
                  *pUpdate = TRUE ;
               }
            }
            else
            {
               PD_LOG( PDWARNING, "Get group info[%u] from remote failed, "
                       "rc: %d", groupID, rc ) ;
               return FALSE ;
            }
         }
         return TRUE ;
      }
      if ( ( isReadCmd && SDB_COORD_REMOTE_DISC == flag ) ||
           SDB_CLS_NODE_NOT_ENOUGH == flag )
      {
         return TRUE ;
      }
      else if ( SDB_CLS_FULL_SYNC == flag ||
                SDB_RTN_IN_REBUILD == flag )
      {
         rtnCoordUpdateNodeStatByRC( cb, nodeID, groupInfo, flag ) ;
         return TRUE ;
      }

      return FALSE ;
   }

   BOOLEAN rtnCoordCataReplyCheck( pmdEDUCB *cb, INT32 flag,
                                   BOOLEAN canRetry,
                                   CoordCataInfoPtr &cataInfo,
                                   BOOLEAN *pUpdate,
                                   BOOLEAN canUpdate )
   {
      if ( canRetry && rtnCoordCataCheckFlag( flag ) )
      {
         if ( canUpdate && cataInfo.get() )
         {
            CoordCataInfoPtr newCataInfo ;
            const CHAR *collectionName = cataInfo->getName() ;
            INT32 rc = rtnCoordGetRemoteCata( cb, collectionName,
                                              newCataInfo ) ;
            if ( SDB_OK == rc )
            {
               cataInfo = newCataInfo ;
               if( pUpdate )
               {
                  *pUpdate = TRUE ;
               }
            }
            else
            {
               PD_LOG( PDWARNING, "Get catalog info[%s] from remote failed, "
                       "rc: %d", collectionName, rc ) ;
               return FALSE ;
            }
         }
         return TRUE ;
      }

      return FALSE ;
   }

   BOOLEAN rtnCoordCataCheckFlag( INT32 flag )
   {
      return ( SDB_CLS_COORD_NODE_CAT_VER_OLD == flag ||
               SDB_CLS_NO_CATALOG_INFO == flag ||
               SDB_CLS_GRP_NOT_EXIST == flag ||
               SDB_CLS_NODE_NOT_EXIST == flag ||
               SDB_CAT_NO_MATCH_CATALOG == flag ) ;
   }

   INT32 rtnCataChangeNtyToAllNodes( pmdEDUCB * cb )
   {
      INT32 rc = SDB_OK ;
      netMultiRouteAgent *pRouteAgent = sdbGetCoordCB()->getRouteAgent() ;
      MsgHeader ntyMsg ;
      ntyMsg.messageLength = sizeof( MsgHeader ) ;
      ntyMsg.opCode = MSG_CAT_GRP_CHANGE_NTY ;
      ntyMsg.requestID = 0 ;
      ntyMsg.routeID.value = 0 ;
      ntyMsg.TID = cb->getTID() ;

      CoordGroupList groupLst ;
      ROUTE_SET sendNodes ;
      REQUESTID_MAP successNodes ;
      ROUTE_RC_MAP failedNodes ;

      rc = rtnCoordGetAllGroupList( cb, groupLst, NULL, FALSE ) ;
      PD_RC_CHECK( rc, PDERROR, "Failed to get all group list, rc: %d", rc ) ;

      rc = rtnCoordGetGroupNodes( cb, BSONObj(), NODE_SEL_ALL,
                                  groupLst, sendNodes ) ;
      PD_RC_CHECK( rc, PDERROR, "Failed to get nodes, rc: %d", rc ) ;
      if ( sendNodes.size() == 0 )
      {
         PD_LOG( PDWARNING, "Not found any node" ) ;
         rc = SDB_CLS_NODE_NOT_EXIST ;
         goto error ;
      }

      rtnCoordSendRequestToNodes( (void*)&ntyMsg, sendNodes, 
                                  pRouteAgent, cb, successNodes,
                                  failedNodes ) ;
      if ( failedNodes.size() != 0 )
      {
         rc = failedNodes.begin()->second._rc ;
      }
      rtnCoordClearRequest( cb, successNodes ) ;

   done:
      return rc ;
   error:
      goto done ;
   }

   INT32 rtnCoordParseControlParam( const BSONObj &obj,
                                    rtnCoordCtrlParam &param,
                                    UINT32 mask,
                                    BSONObj *pNewObj )
   {
      INT32 rc = SDB_OK ;
      BOOLEAN modify = FALSE ;
      BSONObjBuilder builder ;
      const CHAR *tmpStr = NULL ;
      vector< const CHAR* > tmpVecStr ;

      BSONObjIterator it( obj ) ;
      while( it.more() )
      {
         BSONElement e = it.next() ;

         if ( Array == e.type() &&
              0 == ossStrcmp( e.fieldName(), "$and" ) )
         {
            BSONArrayBuilder sub( builder.subarrayStart( e.fieldName() ) ) ;
            BSONObj tmpNew ;
            BSONObjIterator tmpItr( e.embeddedObject() ) ;
            while ( tmpItr.more() )
            {
               BSONElement tmpE = tmpItr.next() ;
               if ( Object != tmpE.type() )
               {
                  PD_LOG( PDERROR, "Parse obj[%s] conrtol param failed: "
                          "invalid $and", obj.toString().c_str() ) ;
                  rc = SDB_INVALIDARG ;
                  goto error ;
               }
               else
               {
                  BSONObj tmpObj = tmpE.embeddedObject() ;
                  rc = rtnCoordParseControlParam( tmpObj, param, mask,
                                                  pNewObj ? &tmpNew : NULL ) ;
                  PD_RC_CHECK( rc, PDERROR, "Parse obj[%s] conrtol param "
                               "failed", obj.toString().c_str() ) ;
                  if ( pNewObj )
                  {
                     if ( tmpNew.objdata() != tmpObj.objdata() )
                     {
                        modify = TRUE ;
                        sub.append( tmpNew ) ;
                     }
                     else
                     {
                        sub.append( tmpObj ) ;
                     }
                  }
               }
            }
            sub.done() ;
         } /// end $and
         else if ( ( mask & RTN_CTRL_MASK_GLOBAL ) &&
                   0 == ossStrcasecmp( e.fieldName(), FIELD_NAME_GLOBAL ) &&
                   SDB_OK == _rtnCoordParseBoolean( e, param._isGlobal,
                                                    RTN_COORD_PARSE_MASK_ET ) )
         {
            modify = TRUE ;
            param._parseMask |= RTN_CTRL_MASK_GLOBAL ;
         }
         else if ( ( mask & RTN_CTRL_MASK_GLOBAL ) &&
                   e.isNull() &&
                   ( 0 == ossStrcasecmp( e.fieldName(),
                                        FIELD_NAME_GROUPS ) ||
                     0 == ossStrcasecmp( e.fieldName(),
                                        FIELD_NAME_GROUPNAME ) ||
                     0 == ossStrcasecmp( e.fieldName(),
                                        FIELD_NAME_GROUPID ) )
                 )
         {
            param._isGlobal = FALSE ;
            modify = TRUE ;
            param._parseMask |= RTN_CTRL_MASK_GLOBAL ;
         }
         else if ( ( mask & RTN_CTRL_MASK_NODE_SELECT ) &&
                   0 == ossStrcasecmp( e.fieldName(),
                                       FIELD_NAME_NODE_SELECT ) &&
                   SDB_OK == _rtnCoordParseString( e, tmpStr,
                                                   RTN_COORD_PARSE_MASK_ET ) )
         {
            modify = TRUE ;
            param._parseMask |= RTN_CTRL_MASK_NODE_SELECT ;
            if ( 0 == ossStrcasecmp( tmpStr, "primary" ) ||
                 0 == ossStrcasecmp( tmpStr, "master" ) ||
                 0 == ossStrcasecmp( tmpStr, "m" ) ||
                 0 == ossStrcasecmp( tmpStr, "p" ) )
            {
               param._emptyFilterSel = NODE_SEL_PRIMARY ;
            }
            else if ( 0 == ossStrcasecmp( tmpStr, "any" ) ||
                      0 == ossStrcasecmp( tmpStr, "a" ) )
            {
               param._emptyFilterSel = NODE_SEL_ANY ;
            }
            else if ( 0 == ossStrcasecmp( tmpStr, "secondary" ) ||
                      0 == ossStrcasecmp( tmpStr, "s" ) )
            {
               param._emptyFilterSel = NODE_SEL_SECONDARY ;
            }
            else
            {
               param._emptyFilterSel = NODE_SEL_ALL ;
            }
         }
         else if ( ( mask & RTN_CTRL_MASK_ROLE ) &&
                   0 == ossStrcasecmp( e.fieldName(),
                                       FIELD_NAME_ROLE ) &&
                   SDB_OK == _rtnCoordParseString( e, tmpVecStr,
                                                   RTN_COORD_PARSE_MASK_ALL ) )
         {
            ossMemset( &param._role, 0, sizeof( param._role ) ) ;
            modify = TRUE ;
            param._parseMask |= RTN_CTRL_MASK_ROLE ;

            for ( UINT32 i = 0 ; i < tmpVecStr.size() ; ++i )
            {
               if ( 0 == ossStrcasecmp( tmpVecStr[i], "all" ) )
               {
                  for ( UINT32 k = 0 ; k < (UINT32)SDB_ROLE_MAX ; ++k )
                  {
                     param._role[ k ] = 1 ;
                  }
                  break ;
               }
               if ( SDB_ROLE_MAX != utilGetRoleEnum( tmpVecStr[ i ] ) )
               {
                  param._role[ utilGetRoleEnum( tmpVecStr[ i ] ) ] = 1 ;
               }
            }
            tmpVecStr.clear() ;
         }
         else if ( ( mask & RTN_CTRL_MASK_RAWDATA ) &&
                   0 == ossStrcasecmp( e.fieldName(), FIELD_NAME_RAWDATA ) &&
                   SDB_OK == _rtnCoordParseBoolean( e, param._rawData,
                                                    RTN_COORD_PARSE_MASK_ET ) )
         {
            modify = TRUE ;
            param._parseMask |= RTN_CTRL_MASK_RAWDATA ;
         }
         else if ( pNewObj )
         {
            builder.append( e ) ;
         }
      }

      if ( pNewObj )
      {
         if ( modify )
         {
            *pNewObj = builder.obj() ;
         }
         else
         {
            *pNewObj = obj ;
         }
      }

   done:
      return SDB_OK ;
   error:
      goto done ;
   }

   BOOLEAN rtnCoordCanRetry( UINT32 retryTimes )
   {
      return retryTimes < RTN_COORD_MAX_RETRYTIMES ? TRUE : FALSE ;
   }

   void rtnBuildFailedNodeReply( ROUTE_RC_MAP &failedNodes,
                                 BSONObjBuilder &builder )
   {
      INT32 rc = SDB_OK ;

      if ( failedNodes.size() > 0 )
      {
         CoordCB *pCoordcb = pmdGetKRCB()->getCoordCB() ;
         CoordGroupInfoPtr groupInfo ;
         string strHostName ;
         string strServiceName ;
         string strNodeName ;
         string strGroupName ;
         MsgRouteID routeID ;
         BSONObj errObj ;
         BSONArrayBuilder arrayBD( builder.subarrayStart(
                                   FIELD_NAME_ERROR_NODES ) ) ;
         ROUTE_RC_MAP::iterator iter = failedNodes.begin() ;
         while ( iter != failedNodes.end() )
         {
            strHostName.clear() ;
            strServiceName.clear() ;
            strNodeName.clear() ;
            strGroupName.clear() ;

            routeID.value = iter->first ;
            rc = pCoordcb->getGroupInfo( routeID.columns.groupID, groupInfo ) ;
            if ( rc )
            {
               PD_LOG( PDWARNING, "Failed to get group[%d] info, rc: %d",
                       routeID.columns.groupID, rc ) ;
            }
            else
            {
               strGroupName = groupInfo->groupName() ;

               routeID.columns.serviceID = MSG_ROUTE_LOCAL_SERVICE ;
               rc = groupInfo->getNodeInfo( routeID, strHostName,
                                            strServiceName ) ;
               if ( rc )
               {
                  PD_LOG( PDWARNING, "Failed to get node[%d] info failed, "
                          "rc: %d", routeID.columns.nodeID, rc ) ;
               }
               else
               {
                  strNodeName = strHostName + ":" + strServiceName ;
               }
            }

            try
            {
               BSONObjBuilder objBD( arrayBD.subobjStart() ) ;
               objBD.append( FIELD_NAME_NODE_NAME, strNodeName ) ;
               objBD.append( FIELD_NAME_GROUPNAME, strGroupName ) ;
               objBD.append( FIELD_NAME_RCFLAG, iter->second._rc ) ;
               objBD.append( FIELD_NAME_ERROR_IINFO, iter->second._obj ) ;
               objBD.done() ;
            }
            catch ( std::exception &e )
            {
               PD_LOG( PDWARNING, "Build error object occur exception: %s",
                       e.what() ) ;
            }
            ++iter ;
         }

         arrayBD.done() ;
      }
   }

   BSONObj rtnBuildErrorObj( INT32 &flag,
                             pmdEDUCB *cb,
                             ROUTE_RC_MAP *pFailedNodes )
   {
      BSONObjBuilder builder ;
      const CHAR *pDetail = "" ;

      if ( SDB_OK == flag && pFailedNodes && pFailedNodes->size() > 0 )
      {
         flag = SDB_COORD_NOT_ALL_DONE ;
      }

      if ( cb && cb->getInfo( EDU_INFO_ERROR ) )
      {
         pDetail = cb->getInfo( EDU_INFO_ERROR ) ;
      }

      builder.append( OP_ERRNOFIELD, flag ) ;
      builder.append( OP_ERRDESP_FIELD, getErrDesp( flag ) ) ;
      builder.append( OP_ERR_DETAIL, pDetail ? pDetail : "" ) ;
      if ( pFailedNodes && pFailedNodes->size() > 0 )
      {
         rtnBuildFailedNodeReply( *pFailedNodes, builder ) ;
      }

      return builder.obj() ;
   }

}

