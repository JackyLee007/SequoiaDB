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

   Source File Name = rtnCoordSnapshot.cpp

   Descriptive Name = Runtime Coord Commands

   When/how to use: this program may be used on binary and text-formatted
   versions of runtime component. This file contains code logic for
   common functions for coordinator node.

   Dependencies: N/A

   Restrictions: N/A

   Change Activity:
   defect Date        Who Description
   ====== =========== === ==============================================
          09-23-2016  XJH Init
   Last Changed =

*******************************************************************************/
#include "rtnCoordSnapshot.hpp"
#include "rtnCoordDef.hpp"
#include "catDef.hpp"

using namespace bson ;

namespace engine
{

   /*
      rtnCoordSnapshotTransCurIntr implement
   */
   void rtnCoordSnapshotTransCurIntr::_preSet( pmdEDUCB *cb,
                                               rtnCoordCtrlParam &ctrlParam )
   {
      ctrlParam._role[ SDB_ROLE_CATALOG ] = 0 ;
      ctrlParam._emptyFilterSel = NODE_SEL_PRIMARY ;

      ctrlParam._useSpecialNode = TRUE ;
      DpsTransNodeMap *pMap = cb->getTransNodeLst() ;
      if ( pMap )
      {
         DpsTransNodeMap::iterator it = pMap->begin() ;
         while( it != pMap->end() )
         {
            ctrlParam._specialNodes.insert( it->second.value ) ;
            ++it ;
         }
      }
   }

   /*
      rtnCoordSnapshotTransIntr implement
   */
   void rtnCoordSnapshotTransIntr::_preSet( pmdEDUCB *cb,
                                            rtnCoordCtrlParam &ctrlParam )
   {
      ctrlParam._role[ SDB_ROLE_CATALOG ] = 0 ;
      ctrlParam._emptyFilterSel = NODE_SEL_PRIMARY ;
   }

   /*
      rtnCoordSnapshotTransCur implement
   */
   const CHAR* rtnCoordSnapshotTransCur::getIntrCMDName()
   {
      return COORD_CMD_SNAPSHOTTRANSCURINTR ;
   }

   const CHAR* rtnCoordSnapshotTransCur::getInnerAggrContent()
   {
      return NULL ;
   }

   /*
      rtnCoordSnapshotTrans implement
   */
   const CHAR* rtnCoordSnapshotTrans::getIntrCMDName()
   {
      return COORD_CMD_SNAPSHOTTRANSINTR ;
   }

   const CHAR* rtnCoordSnapshotTrans::getInnerAggrContent()
   {
      return NULL ;
   }

   /*
      rtnCoordCMDSnapshotDataBase implement
   */
   const CHAR* rtnCoordCMDSnapshotDataBase::getIntrCMDName()
   {
      return COORD_CMD_SNAPSHOTDBINTR;
   }

   const CHAR* rtnCoordCMDSnapshotDataBase::getInnerAggrContent()
   {
      return RTNCOORD_SNAPSHOTDB_INPUT ;
   }

   /*
      rtnCoordCMDSnapshotSystem implement
   */
   const CHAR* rtnCoordCMDSnapshotSystem::getIntrCMDName()
   {
      return COORD_CMD_SNAPSHOTSYSINTR;
   }

   const CHAR* rtnCoordCMDSnapshotSystem::getInnerAggrContent()
   {
      return RTNCOORD_SNAPSHOTSYS_INPUT ;
   }

   /*
      rtnCoordCMDSnapshotCollections implement
   */
   const CHAR* rtnCoordCMDSnapshotCollections::getIntrCMDName()
   {
      return COORD_CMD_SNAPSHOTCLINTR;
   }

   const CHAR* rtnCoordCMDSnapshotCollections::getInnerAggrContent()
   {
      return RTNCOORD_SNAPSHOTCL_INPUT ;
   }

   /*
      rtnCoordCMDSnapshotSpaces implement
   */
   const CHAR* rtnCoordCMDSnapshotSpaces::getIntrCMDName()
   {
      return COORD_CMD_SNAPSHOTCSINTR;
   }

   const CHAR* rtnCoordCMDSnapshotSpaces::getInnerAggrContent()
   {
      return RTNCOORD_SNAPSHOTCS_INPUT ;
   }

   /*
      rtnCoordCMDSnapshotContexts implement
   */
   const CHAR* rtnCoordCMDSnapshotContexts::getIntrCMDName()
   {
      return COORD_CMD_SNAPSHOTCTXINTR;
   }

   const CHAR* rtnCoordCMDSnapshotContexts::getInnerAggrContent()
   {
      return RTNCOORD_SNAPSHOTCONTEXTS_INPUT ;
   }

   /*
      rtnCoordCMDSnapshotContextsCur implement
   */
   const CHAR* rtnCoordCMDSnapshotContextsCur::getIntrCMDName()
   {
      return COORD_CMD_SNAPSHOTCTXCURINTR;
   }

   const CHAR* rtnCoordCMDSnapshotContextsCur::getInnerAggrContent()
   {
      return RTNCOORD_SNAPSHOTCONTEXTSCUR_INPUT ;
   }

   /*
      rtnCoordCMDSnapshotSessions implement
   */
   const CHAR* rtnCoordCMDSnapshotSessions::getIntrCMDName()
   {
      return COORD_CMD_SNAPSHOTSESSINTR;
   }

   const CHAR* rtnCoordCMDSnapshotSessions::getInnerAggrContent()
   {
      return RTNCOORD_SNAPSHOTSESS_INPUT ;
   }

   /*
      rtnCoordCMDSnapshotSessionsCur implement
   */
   const CHAR* rtnCoordCMDSnapshotSessionsCur::getIntrCMDName()
   {
      return COORD_CMD_SNAPSHOTSESSCURINTR;
   }

   const CHAR* rtnCoordCMDSnapshotSessionsCur::getInnerAggrContent()
   {
      return RTNCOORD_SNAPSHOTSESSCUR_INPUT ;
   }

   /*
      rtnCoordCMDSnapshotCata implement
   */
   INT32 rtnCoordCMDSnapshotCata::_preProcess( rtnQueryOptions &queryOpt,
                                               string &clName,
                                               BSONObj &outSelector )
   {
      clName = CAT_COLLECTION_INFO_COLLECTION ;
      return SDB_OK ;
   }

}

