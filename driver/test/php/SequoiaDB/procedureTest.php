<?php
class SequoiaDB_Procedure_Test extends PHPUnit_Framework_TestCase
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
      $this -> assertEquals( 0, $err['errno'], '数据库连接错误( 数组参数: '.$this->address.' )' ) ;
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
   public function test_createJsProcedure( $db, $isStandlone )
   {
      if( $isStandlone == false )
      {
         $err = $db -> createJsProcedure( 'function sum( a,b ){ return a + b ; }' ) ;
         $this -> assertEquals( 0, $err['errno'], 'createJsProcedure错误' ) ;
      }
   }
   
   /**
    * @depends test_connect
    * @depends test_isStandlone
    */
   public function test_listProcedure( $db, $isStandlone )
   {
      if( $isStandlone == false )
      {
         $cursor = $db -> listProcedure( array( 'name' => 'sum' ) ) ;
         $err = $db -> getError() ;
         $this -> assertEquals( 0, $err['errno'], 'listProcedure错误' ) ;
         $this -> assertNotEmpty( $cursor, 'listProcedure错误' ) ;
         $num = 0 ;
         while( $record = $cursor -> next() ) {
            ++$num ;
         }
         $this -> assertEquals( 1, $num, 'listProcedure错误' ) ;
      }
   }
   
   /**
    * @depends test_connect
    * @depends test_isStandlone
    */
   public function test_evalJs( $db, $isStandlone )
   {
      if( $isStandlone == false )
      {
         $result = $db -> evalJs( 'sum( 1, 2 );' ) ;
         $this -> assertEquals( 3, $result, 'evalJs错误' ) ;
      }
   }
   
   /**
    * @depends test_connect
    * @depends test_isStandlone
    */
   public function test_removeProcedure( $db, $isStandlone )
   {
      if( $isStandlone == false )
      {
         $err = $db -> removeProcedure( 'sum' ) ;
         $this -> assertEquals( 0, $err['errno'], 'removeProcedure错误' ) ;
      }
   }
}

?>