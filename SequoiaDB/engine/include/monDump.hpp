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

   Source File Name = monDump.hpp

   Descriptive Name = Monitor Dump Header

   When/how to use: this program may be used on binary and text-formatted
   versions of monitoring component. This file contains declare for
   functions that generate result dataset for given resources.

   Dependencies: N/A

   Restrictions: N/A

   Change Activity:
   defect Date        Who Description
   ====== =========== === ==============================================
          09/14/2012  TW  Initial Draft

   Last Changed =

*******************************************************************************/
#ifndef MONDUMP_HPP_
#define MONDUMP_HPP_

#include "pmdEDU.hpp"
#include "rtnCB.hpp"
#include "rtnContext.hpp"
#include "rtnFetchBase.hpp"
#include "dpsTransCB.hpp"
#include "../bson/bson.h"

using namespace bson ;

namespace engine
{
   /*
      Base define
   */
   #define MON_MASK_NODE_NAME          0x00000001
   #define MON_MASK_HOSTNAME           0x00000002
   #define MON_MASK_SERVICE_NAME       0x00000004
   #define MON_MASK_GROUP_NAME         0x00000008
   #define MON_MASK_IS_PRIMARY         0x00000010
   #define MON_MASK_SERVICE_STATUS     0x00000020
   #define MON_MASK_LSN_INFO           0x00000040
   #define MON_MASK_NODEID             0x00000080
   #define MON_MASK_TRANSINFO          0x00000100
   #define MON_MASK_ALL                0xFFFFFFFF

   #define MON_MASK_FETCH_DEFAULT      ( MON_MASK_NODE_NAME |\
                                         MON_MASK_HOSTNAME |\
                                         MON_MASK_SERVICE_NAME |\
                                         MON_MASK_GROUP_NAME |\
                                         MON_MASK_NODEID )

   INT32 monAppendSystemInfo ( BSONObjBuilder &ob,
                               UINT32 mask = MON_MASK_ALL ) ;

   void  monAppendVersion ( BSONObjBuilder &ob ) ;

   INT32 monDumpIndexes( vector<monIndex> &indexes, rtnContextDump *context ) ;

   INT32 monDumpTraceStatus ( rtnContextDump *context ) ;

   INT32 monDumpDatablocks( std::vector<dmsExtentID> &datablocks,
                            rtnContextDump *context ) ;

   INT32 monDumpIndexblocks( std::vector< BSONObj > &idxBlocks,
                             std::vector< dmsRecordID > &idxRIDs,
                             const CHAR *indexName,
                             dmsExtentID indexLID,
                             INT32 direction,
                             rtnContextDump *context ) ;

   void  monResetMon () ;

   INT32 monDBDumpStorageInfo( BSONObjBuilder &ob );

   INT32 monDBDumpProcMemInfo( BSONObjBuilder &ob );

   INT32 monDBDumpNetInfo( BSONObjBuilder &ob );

   INT32 monDBDumpLogInfo( BSONObjBuilder &ob );

   INT32 monDumpLastOpInfo( BSONObjBuilder &ob, const monAppCB &moncb ) ;

   /*
      _monTransFetcher define
   */
   class _monTransFetcher : public rtnFetchBase
   {
      DECLARE_FETCH_AUTO_REGISTER()

      public:
         _monTransFetcher() ;
         virtual ~_monTransFetcher() ;

         virtual INT32        init( pmdEDUCB *cb,
                                    BOOLEAN isCurrent,
                                    BOOLEAN isDetail,
                                    UINT32 addInfoMask,
                                    const BSONObj obj = BSONObj() ) ;

         virtual const CHAR*  getName() const ;

      public:
         virtual BOOLEAN   isHitEnd() const ;
         virtual INT32     fetch( BSONObj &obj ) ;

      protected:
         INT32    _fetchNextTransInfo() ;

      private:
         TRANS_EDU_LIST             _eduList ;
         monTransInfo               _curTransInfo ;
         DpsTransCBLockList::iterator _pos ;
         BSONObj                    _curEduInfo ;
         BOOLEAN                    _dumpCurrent ;
         BOOLEAN                    _detail ;
         UINT32                     _addInfoMask ;
         BOOLEAN                    _hitEnd ;
         UINT32                     _slice ;
   } ;
   typedef _monTransFetcher monTransFetcher ;

   /*
      _monContextFetcher define
   */
   class _monContextFetcher : public rtnFetchBase
   {
      DECLARE_FETCH_AUTO_REGISTER()

      public:
         _monContextFetcher() ;
         virtual ~_monContextFetcher() ;

         virtual INT32        init( pmdEDUCB *cb,
                                    BOOLEAN isCurrent,
                                    BOOLEAN isDetail,
                                    UINT32 addInfoMask,
                                    const BSONObj obj = BSONObj() ) ;

         virtual const CHAR*  getName() const ;

      public:
         virtual BOOLEAN   isHitEnd() const ;
         virtual INT32     fetch( BSONObj &obj ) ;

      protected:
         INT32       _fetchNextSimple( BSONObj &obj ) ;
         INT32       _fetchNextDetail( BSONObj &obj ) ;

      private:
         BOOLEAN                    _dumpCurrent ;
         BOOLEAN                    _detail ;
         UINT32                     _addInfoMask ;
         BOOLEAN                    _hitEnd ;

         std::map<UINT64, std::set<SINT64> >          _contextList ;
         std::map<UINT64, std::set<monContextFull> >  _contextInfoList ;
   } ;
   typedef _monContextFetcher monContextFetcher ;

   /*
      _monSessionFetcher define
   */
   class _monSessionFetcher : public rtnFetchBase
   {
      DECLARE_FETCH_AUTO_REGISTER()

      public:
         _monSessionFetcher() ;
         virtual ~_monSessionFetcher() ;

         virtual INT32        init( pmdEDUCB *cb,
                                    BOOLEAN isCurrent,
                                    BOOLEAN isDetail,
                                    UINT32 addInfoMask,
                                    const BSONObj obj = BSONObj() ) ;

         virtual const CHAR*  getName() const ;

      public:
         virtual BOOLEAN   isHitEnd() const ;
         virtual INT32     fetch( BSONObj &obj ) ;

      protected:
         INT32       _fetchNextSimple( BSONObj &obj ) ;
         INT32       _fetchNextDetail( BSONObj &obj ) ;

      private:
         BOOLEAN                    _dumpCurrent ;
         BOOLEAN                    _detail ;
         UINT32                     _addInfoMask ;
         BOOLEAN                    _hitEnd ;

         std::set<monEDUSimple>     _setInfoSimple ;
         std::set<monEDUFull>       _setInfoDetail ;
   } ;
   typedef _monSessionFetcher monSessionFetcher ;

   /*
      _monCollectionFetch define
   */
   class _monCollectionFetch : public rtnFetchBase
   {
      DECLARE_FETCH_AUTO_REGISTER()

      public:
         _monCollectionFetch() ;
         virtual ~_monCollectionFetch() ;
         /*
            Use isCurrent for isIncludeSystem
         */
         virtual INT32        init( pmdEDUCB *cb,
                                    BOOLEAN isCurrent,
                                    BOOLEAN isDetail,
                                    UINT32 addInfoMask,
                                    const BSONObj obj = BSONObj() ) ;

         virtual const CHAR*  getName() const ;

      public:
         virtual BOOLEAN   isHitEnd() const ;
         virtual INT32     fetch( BSONObj &obj ) ;

      protected:
         INT32       _fetchNextSimple( BSONObj &obj ) ;
         INT32       _fetchNextDetail( BSONObj &obj ) ;

      private:
         BOOLEAN                    _detail ;
         BOOLEAN                    _includeSys ;
         UINT32                     _addInfoMask ;
         BOOLEAN                    _hitEnd ;

         std::set< monCLSimple >    _collectionList ;
         std::set< monCollection >  _collectionInfo ;
   } ;
   typedef _monCollectionFetch monCollectionFetch ;

   /*
      _monCollectionSpaceFetch define
   */
   class _monCollectionSpaceFetch : public rtnFetchBase
   {
      DECLARE_FETCH_AUTO_REGISTER()

      public:
         _monCollectionSpaceFetch() ;
         virtual ~_monCollectionSpaceFetch() ;
         /*
            Use isCurrent for isIncludeSystem
         */
         virtual INT32        init( pmdEDUCB *cb,
                                    BOOLEAN isCurrent,
                                    BOOLEAN isDetail,
                                    UINT32 addInfoMask,
                                    const BSONObj obj = BSONObj() ) ;

         virtual const CHAR*  getName() const ;

      public:
         virtual BOOLEAN   isHitEnd() const ;
         virtual INT32     fetch( BSONObj &obj ) ;

      protected:
         INT32       _fetchNextSimple( BSONObj &obj ) ;
         INT32       _fetchNextDetail( BSONObj &obj ) ;

      private:
         BOOLEAN                    _detail ;
         BOOLEAN                    _includeSys ;
         UINT32                     _addInfoMask ;
         BOOLEAN                    _hitEnd ;

         std::set< monCSSimple >          _csList ;
         std::set< monCollectionSpace >   _csInfo ;
   } ;
   typedef _monCollectionSpaceFetch monCollectionSpaceFetch ;

   /*
      _monDataBaseFetch define
   */
   class _monDataBaseFetch : public rtnFetchBase
   {
      DECLARE_FETCH_AUTO_REGISTER()

      public:
         _monDataBaseFetch() ;
         virtual ~_monDataBaseFetch() ;

         virtual INT32        init( pmdEDUCB *cb,
                                    BOOLEAN isCurrent,
                                    BOOLEAN isDetail,
                                    UINT32 addInfoMask,
                                    const BSONObj obj = BSONObj() ) ;

         virtual const CHAR*  getName() const ;

      public:
         virtual BOOLEAN   isHitEnd() const ;
         virtual INT32     fetch( BSONObj &obj ) ;

      private:
         UINT32                  _addInfoMask ;
         BOOLEAN                 _hitEnd ;

   } ;
   typedef _monDataBaseFetch monDataBaseFetch ;

   /*
      _monSystemFetch define
   */
   class _monSystemFetch : public rtnFetchBase
   {
      DECLARE_FETCH_AUTO_REGISTER()

      public:
         _monSystemFetch() ;
         virtual ~_monSystemFetch() ;

         virtual INT32        init( pmdEDUCB *cb,
                                    BOOLEAN isCurrent,
                                    BOOLEAN isDetail,
                                    UINT32 addInfoMask,
                                    const BSONObj obj = BSONObj() ) ;

         virtual const CHAR*  getName() const ;

      public:
         virtual BOOLEAN   isHitEnd() const ;
         virtual INT32     fetch( BSONObj &obj ) ;

      private:
         UINT32                  _addInfoMask ;
         BOOLEAN                 _hitEnd ;

   } ;
   typedef _monSystemFetch monSystemFetch ;
 
   /*
      _monStorageUnitFetch define
   */
   class _monStorageUnitFetch : public rtnFetchBase
   {
      DECLARE_FETCH_AUTO_REGISTER()

      public:
         _monStorageUnitFetch() ;
         virtual ~_monStorageUnitFetch() ;

         /*
            use isCurrent for isIncludeSystem
         */
         virtual INT32        init( pmdEDUCB *cb,
                                    BOOLEAN isCurrent,
                                    BOOLEAN isDetail,
                                    UINT32 addInfoMask,
                                    const BSONObj obj = BSONObj() ) ;

         virtual const CHAR*  getName() const ;

      public:
         virtual BOOLEAN   isHitEnd() const ;
         virtual INT32     fetch( BSONObj &obj ) ;

      protected:
         INT32       _fetchNext( BSONObj &obj ) ;

      private:
         BOOLEAN                 _includeSys ;
         UINT32                  _addInfoMask ;
         BOOLEAN                 _hitEnd ;

         std::set<monStorageUnit>   _suInfo ;
   } ;
   typedef _monStorageUnitFetch monStorageUnitFetch ;

   /*
      _monIndexFetch define
   */
   class _monIndexFetch : public rtnFetchBase
   {
      DECLARE_FETCH_AUTO_REGISTER()

      public:
         _monIndexFetch() ;
         virtual ~_monIndexFetch() ;

         virtual INT32        init( pmdEDUCB *cb,
                                    BOOLEAN isCurrent,
                                    BOOLEAN isDetail,
                                    UINT32 addInfoMask,
                                    const BSONObj obj = BSONObj() ) ;

         virtual const CHAR*  getName() const ;

      public:
         virtual BOOLEAN   isHitEnd() const ;
         virtual INT32     fetch( BSONObj &obj ) ;

      protected:
         INT32       _fetchNext( BSONObj &obj ) ;

      private:
         UINT32                  _addInfoMask ;
         BOOLEAN                 _hitEnd ;

         UINT32                  _pos ;
         vector<monIndex>        _indexInfo ;
   } ;
   typedef _monIndexFetch monIndexFetch ;

   /*
      _monCLBlockFetch define
   */
   class _monCLBlockFetch : public rtnFetchBase
   {
      DECLARE_FETCH_AUTO_REGISTER()

      public:
         _monCLBlockFetch() ;
         virtual ~_monCLBlockFetch() ;

         virtual INT32        init( pmdEDUCB *cb,
                                    BOOLEAN isCurrent,
                                    BOOLEAN isDetail,
                                    UINT32 addInfoMask,
                                    const BSONObj obj = BSONObj() ) ;

         virtual const CHAR*  getName() const ;

      public:
         virtual BOOLEAN   isHitEnd() const ;
         virtual INT32     fetch( BSONObj &obj ) ;

      protected:
         INT32       _fetchNext( BSONObj &obj ) ;

      private:
         UINT32                  _addInfoMask ;
         BOOLEAN                 _hitEnd ;

         UINT32                  _pos ;
         vector<dmsExtentID>     _vecBlock ;
   } ;
   typedef _monCLBlockFetch monCLBlockFetch ;

   /*
      _monBackupFetch define
   */
   class _monBackupFetch : public rtnFetchBase
   {
      DECLARE_FETCH_AUTO_REGISTER()

      public:
         _monBackupFetch() ;
         virtual ~_monBackupFetch() ;

         virtual INT32        init( pmdEDUCB *cb,
                                    BOOLEAN isCurrent,
                                    BOOLEAN isDetail,
                                    UINT32 addInfoMask,
                                    const BSONObj obj = BSONObj() ) ;

         virtual const CHAR*  getName() const ;

      public:
         virtual BOOLEAN   isHitEnd() const ;
         virtual INT32     fetch( BSONObj &obj ) ;

      protected:
         INT32       _fetchNext( BSONObj &obj ) ;

      private:
         UINT32                  _addInfoMask ;
         BOOLEAN                 _hitEnd ;

         UINT32                  _pos ;
         vector < BSONObj >      _vecBackup ;
   } ;
   typedef _monBackupFetch monBackupFetch ;

}

#endif //MONDUMP_HPP_

