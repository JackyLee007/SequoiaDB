<?php
class SequoiaDB_Transaction_Test extends PHPUnit_Framework_TestCase
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
   public function test_transactionBegin_transactionCommit( $db )
   {
      $cs = $db -> selectCS( 'foo' ) ;
      $err = $db -> getError() ;
      $this -> assertEquals( 0, $err['errno'], '获取cs错误' ) ;
      $this -> assertNotEmpty( $cs, '获取cs错误' ) ;
      
      $cl = $cs -> selectCL( 'bar', array( 'ReplSize' => -1 ) ) ;
      $err = $db -> getError() ;
      $this -> assertEquals( 0, $err['errno'], '获取cl错误' ) ;
      $this -> assertNotEmpty( $cl, '获取cl错误' ) ;

      $err = $db -> transactionBegin() ;
      if( $err['errno'] != -253 )
      {
         $this -> assertEquals( 0, $err['errno'], 'transactionBegin错误' ) ;
         $err = $cl -> insert( array( 'a' => 1 ) ) ;
         if( $err['errno'] != -253 )
         {
            $this -> assertEquals( 0, $err['errno'], 'insert错误' ) ;
         
            $err = $db -> transactionCommit() ;
            $this -> assertEquals( 0, $err['errno'], 'transactionCommit错误' ) ;
         }
      }
   }

   /**
    * @depends test_connect
    */
   public function test_transactionRollback( $db )
   {
      $cs = $db -> selectCS( 'foo' ) ;
      $err = $db -> getError() ;
      $this -> assertEquals( 0, $err['errno'], '获取cs错误' ) ;
      $this -> assertNotEmpty( $cs, '获取cs错误' ) ;
      
      $cl = $cs -> selectCL( 'bar', array( 'ReplSize' => -1 ) ) ;
      $err = $db -> getError() ;
      $this -> assertEquals( 0, $err['errno'], '获取cl错误' ) ;
      $this -> assertNotEmpty( $cl, '获取cl错误' ) ;
      
      $cl -> remove() ;
      
      $num = $cl -> count() ;
      $err = $db -> getError() ;
      $this -> assertEquals( 0, $err['errno'], 'count错误' ) ;

      $err = $db -> transactionBegin() ;
      if( $err['errno'] != -253 )
      {
         $this -> assertEquals( 0, $err['errno'], 'transactionBegin错误' ) ;
         
         $err = $cl -> insert( array( 'a' => 1 ) ) ;
         if( $err['errno'] != -253 )
         {
            $this -> assertEquals( 0, $err['errno'], 'insert错误' ) ;
            
            $err = $db -> transactionRollback() ;
            $this -> assertEquals( 0, $err['errno'], 'transactionRollback错误' ) ;
            
            $num2 = $cl -> count() ;
            $err = $db -> getError() ;
            $this -> assertEquals( 0, $err['errno'], 'count错误' ) ;
            
            $this -> assertEquals( $num, $num2, 'transactionRollback错误' ) ;
         }
      }
   }
}

?>