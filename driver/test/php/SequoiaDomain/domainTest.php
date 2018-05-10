<?php
class Domain_Test extends PHPUnit_Framework_TestCase
{
   protected $hostname ;
   protected $port ;
   protected $address ;

   protected function setUp()
   {
      $this -> hostname = empty( $_POST['hostname'] ) ? '127.0.0.1' : $_POST['hostname'] ;
      $this -> port = empty( $_POST['port'] ) ? '11810' : $_POST['port'] ;
      $this -> address = $this -> hostname.':'.$this -> port ;
   }

   public function test_connect()
   {
      $db = new SequoiaDB();
      $err = $db -> connect( $this->address ) ;
      $this -> assertEquals( 0, $err['errno'], '数据库连接错误( 参数: '.$this->address.' )' ) ;
      return $db ;
   }
   
   /**
    * @depends test_connect
    */
   public function test_isStandlone( $db )
   {
      $cursor = $db -> list( SDB_LIST_SHARDS ) ;
      $err = $db -> getError() ;
      if( $err['errno'] == -159 )
      {
         return true ;
      }
      $this -> assertEquals( 0, $err['errno'], 'list错误' ) ;
      $this -> assertNotEmpty( $cursor, 'list错误' ) ;
      return false ;
   }
   
   /**
    * @depends test_connect
    * @depends test_isStandlone
    */
   public function test_getDomain( $db, $isStandlone )
   {
      $domain = null ;
      if( $isStandlone == false )
      {
         $err = $db -> createDomain( 'myDomain' ) ;
         $this -> assertEquals( 0, $err['errno'], 'createDomain错误' ) ;
         $domain = $db -> getDomain( 'myDomain' ) ;
         $err = $db -> getError() ;
         $this -> assertEquals( 0, $err['errno'], 'getDomain错误' ) ;
         $this -> assertNotEmpty( $domain, 'getDomain错误' ) ;
      }
      return $domain ;
   }
    
   /**
    * @depends test_connect
    * @depends test_getDomain
    * @depends test_isStandlone
    */
   public function test_alter( $db, $domain, $isStandlone )
   {
      if( $isStandlone == false )
      {
         $err = $domain -> alter( array( 'AutoSplit' => true ) ) ;
         $this -> assertEquals( 0, $err['errno'], 'alter错误' ) ;
      }
   }

   /**
    * @depends test_connect
    * @depends test_getDomain
    * @depends test_isStandlone
    */
   public function test_listCS( $db, $domain, $isStandlone )
   {
      if( $isStandlone == false )
      {
         $cursor = $domain -> listCS() ;
         $err = $db -> getError() ;
         $this -> assertEquals( 0, $err['errno'], 'listCS错误' ) ;
         $this -> assertNotEmpty( $cursor, 'listCS错误' ) ;
         while( $record = $cursor -> next() ) {
         }
      }
   }

   /**
    * @depends test_connect
    * @depends test_getDomain
    * @depends test_isStandlone
    */
   public function test_listCL( $db, $domain, $isStandlone )
   {
      if( $isStandlone == false )
      {
         $cursor = $domain -> listCL() ;
         $err = $db -> getError() ;
         $this -> assertEquals( 0, $err['errno'], 'listCL错误' ) ;
         $this -> assertNotEmpty( $cursor, 'listCL错误' ) ;
         while( $record = $cursor -> next() ) {
         }
      }
   }
   
   /**
    * @depends test_connect
    * @depends test_getDomain
    * @depends test_isStandlone
    */
   public function test_listGroup( $db, $domain, $isStandlone )
   {
      if( $isStandlone == false )
      {
         $cursor = $domain -> listGroup() ;
         $err = $db -> getError() ;
         $this -> assertEquals( 0, $err['errno'], 'listGroup错误' ) ;
         $this -> assertNotEmpty( $cursor, 'listGroup错误' ) ;
         while( $record = $cursor -> next() ) {
         }
      }
   }
   
   /**
    * @depends test_connect
    * @depends test_isStandlone
    */
   public function test_clear( $db, $isStandlone )
   {
      if( $isStandlone == false )
      {
         $err = $db -> dropDomain( 'myDomain' ) ;
         $this -> assertEquals( 0, $err['errno'], 'dropDomain错误' ) ;
      }
   }
}
?>