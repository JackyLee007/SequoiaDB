(function(){
   var sacApp = window.SdbSacManagerModule ;
   //控制器
   sacApp.controllerProvider.register( 'Monitor.HostList.HostsNature.Ctrl', function( $scope, $compile, $location, $timeout, SdbRest, SdbFunction ){
      
      _IndexPublic.checkMonitorEdition( $location ) ; //检测是不是企业版

      var clusterName = SdbFunction.LocalData( 'SdbClusterName' ) ;
      var moduleType = SdbFunction.LocalData( 'SdbModuleType' ) ;
      var moduleMode = SdbFunction.LocalData( 'SdbModuleMode' ) ;
      var moduleName = SdbFunction.LocalData( 'SdbModuleName' ) ;
      if( clusterName == null || moduleType != 'sequoiadb' || moduleMode == null || moduleName == null )
      {
         $location.path( '/Transfer' ).search( { 'r': new Date().getTime() } ) ;
         return;
      }
     
      //初始化
      var isFirst = true ;       //是否第一次加载
      var isBuildInfo = false ;  //是否已经初始化好数据
      var lastCPU = [] ;          //每一台主机的cpu性能信息
      //上一次的值
      $scope.LastValue = [] ;
      //主机列表的表格
      $scope.HostTable = {
         'title': {
            'Status':      'Status',
            'HostName':    'HostName',
            'IP':          'IP',
            'CPUUsed':     'CPU',
            'Memory':      'Memory',
            'Disk':        'Disk',
            'NetInValue':  'Network In',
            'NetOutValue': 'Network Out',
            'NetInPackets': 'Network PIn',
            'NetOutPackets': 'Network OIn'
         },
         'body': [],
         'options': {
            'width': {
               'Status':   '80px'
            },
            'sort': {
               'Status':      true,
               'HostName':    true,
               'IP':          true,
               'CPUUsed':     true,
               'Memory':      true,
               'Disk':        true,
               'NetInValue':  true,
               'NetOutValue': true,
               'NetInPackets': true,
               'NetOutPackets': true
            },
            'max': 50,
            'filter': {
               'Status': [
                  { 'key': $scope.autoLanguage( '全部' ), 'value': '' },
                  { 'key': $scope.autoLanguage( '正常' ), 'value': true },
                  { 'key': $scope.autoLanguage( '异常' ), 'value': false }
               ],
               'HostName':    'indexof',
               'IP':          'indexof',
               'CPUUsed':     'indexof',
               'Memory':      'indexof',
               'Disk':        'indexof',
               'NetInValue':  'number',
               'NetOutValue': 'number',
               'NetInPackets': 'number',
               'NetOutPackets': 'number'
            }
         },
         'callback': {}
      } ;
      //定时器
      $scope.Timer = {
         'config': {
            interval: 5
         },
         'callback': {}
      } ;
      //实时刷新设置 弹窗
      $scope.CreateBrush = {
         'config': {},
         'callback': {}
      } ;
      //显示列 下拉菜单
      $scope.FieldDropdown = {
         'config': [
            { 'key': 'Status',         'field': 'Status',       'show': true },
            { 'key': 'HostName',       'field': 'HostName',     'show': true },
            { 'key': 'IP',             'field': 'IP',           'show': true },
            { 'key': 'CPUUsed',        'field': 'CPU',          'show': true },
            { 'key': 'Memory',         'field': 'Memory',       'show': true },
            { 'key': 'Disk',           'field': 'Disk',         'show': true },
            { 'key': 'NetInValue',     'field': 'Network In',   'show': true },
            { 'key': 'NetOutValue',    'field': 'Network Out',  'show': true },
            { 'key': 'NetInPackets',   'field': 'Network PIn',  'show': true },
            { 'key': 'NetOutPackets',  'field': 'Network OIn',  'show': true }
         ],
         'callback': {}
      } ;

      //打开 显示列 下拉菜单
      $scope.OpenShowFieldDropdown = function( event ){
         $.each( $scope.FieldDropdown['config'], function( index, fieldInfo ){
            $scope.FieldDropdown['config'][index]['show'] = typeof( $scope.HostTable['title'][fieldInfo['key']] ) == 'string' ;
         } ) ;
         $scope.FieldDropdown['callback']['Open']( event.currentTarget ) ;
      }

      //保存 显示列
      $scope.SaveField = function(){
         $.each( $scope.FieldDropdown['config'], function( index, fieldInfo ){
            $scope.HostTable['title'][fieldInfo['key']] = fieldInfo['show'] ? fieldInfo['field'] : false ;
         } ) ;
         $scope.FieldDropdown['callback']['Close']() ;
         $scope.HostTable['callback']['ShowCurrentPage']() ;
      }

      //获取主机状态
      var getHostStatus = function( hostnameList, NewHostList ){
         var data = {
            'cmd': 'query host status',
            'HostInfo': JSON.stringify( { 'HostInfo':  hostnameList } )
         } ;
         SdbRest.OmOperation( data, {
            'success': function( hostList ){
               if( hostList.length > 0 )
               {
                  $.each( hostList[0]['HostInfo'], function( index, hostInfo ){
                     NewHostList[index]['NetInValue'] = 0 ;
                     NewHostList[index]['NetOutValue'] = 0 ;
                     NewHostList[index]['NetInPackets'] = 0 ;
                     NewHostList[index]['NetOutPackets'] = 0 ;
                     lastCPU.push( null ) ;
                     if( isNaN( hostInfo['errno'] ) == false )
                     {
                        NewHostList[index]['Status'] = false ;
                        NewHostList[index]['CPUUsed'] = '-' ;
                        NewHostList[index]['Memory'] = '-' ;
                        NewHostList[index]['Disk'] = '-' ;
                        NewHostList[index]['NetInValue'] = '-' ;
                        NewHostList[index]['NetOutValue'] = '-' ;
                        NewHostList[index]['NetInPackets'] = '-' ;
                        NewHostList[index]['NetOutPackets'] = '-' ;
                        NewHostList[index]['Flag'] = hostInfo['errno'] ;
                        return true ;
                     }
                     
                     var DiskSize = 0 ;
                     var DiskFree = 0 ;
                     $.each( hostInfo['Disk'], function( index3, diskInfo ){
                        DiskSize += diskInfo['Size'] ;
                        DiskFree += diskInfo['Free'] ;
                     } ) ;

                     //计算CPU
                     if( lastCPU[index] === null )
                     {
                        lastCPU[index] = getCpuUsePercent( [ hostInfo ] ) ;
                        NewHostList[index]['CPUUsed'] = 0 ;
                     }
                     else
                     {
                        NewHostList[index]['CPUUsed'] = getCpuUsePercent( [ hostInfo ], lastCPU[index] ) ;
                     }

                     //计算网卡 - 全量
                     $.each( hostInfo['Net']['Net'], function( netIndex, netInfo ){
                        NewHostList[index]['NetInValue'] += netInfo['RXBytes']['Megabit'] + netInfo['RXBytes']['Unit'] / 1024 / 1024 ;
                        NewHostList[index]['NetOutValue'] = netInfo['TXBytes']['Megabit'] + netInfo['TXBytes']['Unit'] / 1024 / 1024 ;
                        NewHostList[index]['NetInPackets'] += netInfo['RXPackets']['Megabit'] * 1024 * 1024 + netInfo['RXPackets']['Unit'] ;
                        NewHostList[index]['NetOutPackets'] += netInfo['TXPackets']['Megabit'] * 1024 * 1024 + netInfo['TXPackets']['Unit'] ;
                     } ) ;

                     NewHostList[index]['Status'] = true ;
                     NewHostList[index]['NetOutValue'] = fixedNumber( NewHostList[index]['NetOutValue'], 2 ) ;
                     NewHostList[index]['NetInValue'] = fixedNumber( NewHostList[index]['NetInValue'], 2 ) ;


                     NewHostList[index]['DiskSize'] = fixedNumber( DiskSize / 1024, 2 ) ;
                     NewHostList[index]['DiskUsed'] = fixedNumber( ( DiskSize - DiskFree ) / 1024, 2 ) ;
                     NewHostList[index]['MemorySize'] = fixedNumber( hostInfo['Memory']['Size'] / 1024, 2 ) ;
                     NewHostList[index]['MemoryUsed'] = fixedNumber( hostInfo['Memory']['Used'] / 1024, 2 ) ;

                     NewHostList[index]['Memory'] = NewHostList[index]['MemoryUsed'] + 'GB / ' + NewHostList[index]['MemorySize'] + 'GB' ;
                     NewHostList[index]['Disk'] = NewHostList[index]['DiskUsed'] + 'GB / ' + NewHostList[index]['DiskSize'] + 'GB' ;
                  } ) ;

                  $scope.LastValue = $scope.HostTable['body'] ;
                  $scope.HostTable['body'] = NewHostList ;

                  if( isFirst == true )   //第一次加载要加载两次, 获取两次来计算cpu
                  {
                     $scope.LastValue = NewHostList ;
                     isFirst = false ;
                     $timeout( function(){
                        getHostList( hostnameList ) ;
                     }, 1000 ) ;
                  }
                  else if( isBuildInfo == false )  //之后加载就是开了自动刷新，那么就要开始比对数据了
                  {
                     $scope.LastValue = NewHostList ;
                     isBuildInfo = true ;
                  }
                  else if( $scope.Timer['callback']['GetStatus']() == 'start' || $scope.Timer['callback']['GetStatus']() == 'complete' ) //如果开了定时器，就开始
                  {
                     $scope.Timer['callback']['Complete']() ;
                  }
               }
            },
            'failed': function( errorInfo ){
               _IndexPublic.createRetryModel( $scope, errorInfo, function(){
                  getHostStatus( hostnameList, NewHostList ) ;
                  return true ;
               } ) ;
            }
         }, { 'showLoading':false } ) ;
      }

      //获取主机对应的IP
      var getHostList = function( hostnameList ){
         var data = {
            'cmd': 'query host',
            'filter': JSON.stringify( { '$or': hostnameList } ),
            'sort': JSON.stringify( { 'HostName': 1 } ),
            'selector': JSON.stringify( { 'HostName': null, 'IP': null } )
         } ;
         SdbRest.OmOperation( data, {
            'success': function( hostList ){
               var NewHostList = [] ;
               $.each( hostList, function( index, hostInfo ){
                  NewHostList.push( {
                     'i': index,
                     'Status': true,
                     'Flag': '',
                     'HostName': hostInfo['HostName'],
                     'IP': hostInfo['IP'],
                     'DiskSize': 0,
                     'DiskUsed': 0,
                     'MemorySize': 0,
                     'MemoryUsed': 0,
                     'CPUUsed': 0,
                     'NetInValue' : 0,
                     'NetOutValue' : 0
                  } ) ;
               } ) ;
               getHostStatus( hostnameList, NewHostList ) ;
            },
            'failed': function( errorInfo ){
               _IndexPublic.createRetryModel( $scope, errorInfo, function(){
                  getHostList( hostnameList ) ;
                  return true ;
               } ) ;
            }
         },  { 'showLoading':false } ) ;
      } ;

      //获取业务下的主机列表
      var getModuleInfo = function(){
         var data = {
            'cmd': 'query business',
            'filter' : JSON.stringify( { 'BusinessName': moduleName } )
         } ;
         SdbRest.OmOperation( data, {
            'success': function( moduleInfo ){
               var hostnameList = [] ;
               if( moduleInfo.length > 0 )
               {
                  $.each( moduleInfo[0]['Location'], function( index, hostName ){
                     hostnameList.push( { 'HostName': hostName['HostName'] } ) ;
                  } ) ;
                  getHostList( hostnameList ) ;
               }
            },
            'failed': function( errorInfo ){
               _IndexPublic.createRetryModel( $scope, errorInfo, function(){
                  getModuleInfo() ;
                  return true ;
               } ) ;
            }
         }, { 'showLoading':false } ) ;
      } ;

      getModuleInfo() ;

      //打开 实时刷新 弹窗
      $scope.OpenBrushWindows = function(){
         //表单参数
         var brushForm = {
            'inputList': [
               {
                  "name": "play",
                  "webName": $scope.autoLanguage( '自动刷新' ),
                  "type": "select",
                  "value": $scope.Timer['callback']['GetStatus']() != 'stop',
                  "valid": [
                     { 'key': $scope.autoLanguage( '开启' ), 'value': true },
                     { 'key': $scope.autoLanguage( '停止' ), 'value': false }
                  ]
               },
               {
                  "name": "interval",
                  "webName": $scope.autoLanguage( '刷新间距(秒)' ),
                  "type": "int",
                  "value": $scope.Timer['callback']['GetInterval'](),
                  "valid": {
                     'min': 1
                  }
               }
            ]
         } ;
         $scope.CreateBrush['config'] = brushForm ;
         //设置确定按钮
         $scope.CreateBrush['callback']['SetOkButton']( $scope.autoLanguage( '确定' ), function(){
            var isAllClear = brushForm.check() ;
            if( isAllClear )
            {
               var formVal = brushForm.getValue() ;
               $scope.Timer['callback']['SetInterval']( formVal['interval'] ) ;
               if( formVal['play'] == true )
               {
                  $scope.Timer['callback']['Start']( getModuleInfo ) ;
               }
               else
               {
                  $scope.Timer['callback']['Stop']() ;
               }
            }
            return isAllClear ;
         } ) ;
         //设置标题
         $scope.CreateBrush['callback']['SetTitle']( $scope.autoLanguage( '实时刷新设置' ) ) ;
         //设置图标
         $scope.CreateBrush['callback']['SetIcon']( '' ) ;
         //打开窗口
         $scope.CreateBrush['callback']['Open']() ;
      }
      
      //跳转事件
      $scope.GotoHost = function( HostName ){
         SdbFunction.LocalData( 'SdbHostName', HostName ) ;
         $location.path( '/Monitor/SDB-Host/Info/Index' ).search( { 'r': new Date().getTime() } ) ;
      } ;

      $scope.GotoDisk = function( HostName ){
         SdbFunction.LocalData( 'SdbHostName', HostName ) ;
         $location.path( '/Monitor/SDB-Host/Info/Disk' ).search( { 'r': new Date().getTime() } ) ;
      } ;

      $scope.GotoNet = function( HostName ){
         SdbFunction.LocalData( 'SdbHostName', HostName ) ;
         $location.path( '/Monitor/SDB-Host/Info/Network' ).search( { 'r': new Date().getTime() } ) ;
      } ;

      $scope.GotoCPU = function( HostName ){
         SdbFunction.LocalData( 'SdbHostName', HostName ) ;
         $location.path( '/Monitor/SDB-Host/Info/CPU' ).search( { 'r': new Date().getTime() } ) ;
      } ;

      $scope.GotoMemory = function( HostName ){
         SdbFunction.LocalData( 'SdbHostName', HostName ) ;
         $location.path( '/Monitor/SDB-Host/Info/Memory' ).search( { 'r': new Date().getTime() } ) ;
      } ;

      //跳转至资源
      $scope.GotoResources = function(){
         $location.path( '/Monitor/SDB-Resources/Session' ).search( { 'r': new Date().getTime() } ) ;
      } ;

      //跳转至主机列表
      $scope.GotoHosts = function(){
         $location.path( '/Monitor/SDB-Host/List/Index' ).search( { 'r': new Date().getTime() } ) ;
      } ;
      
      //跳转至节点列表
      $scope.GotoNodes = function(){
         if( moduleMode == 'distribution' )
         {
            $location.path( '/Monitor/SDB-Nodes/Nodes' ).search( { 'r': new Date().getTime() } ) ;
         }
         else
         {
            $location.path( '/Monitor/SDB-Nodes/Node/Index' ).search( { 'r': new Date().getTime() } ) ;
         }
      } ;
   } ) ;
}());