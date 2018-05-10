package com.sequoiadb.test.decimal;

import static org.junit.Assert.*;
import static org.junit.Assert.assertEquals;

import java.math.BigDecimal;
import java.math.BigInteger;
import java.math.MathContext;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;

import org.bson.BSONObject;
import org.bson.BasicBSONObject;
import org.bson.types.BSONDecimal;
import org.bson.types.BSONTimestamp;
import org.bson.types.ObjectId;
import org.junit.After;
import org.junit.AfterClass;
import org.junit.Assert;
import org.junit.Before;
import org.junit.BeforeClass;
import org.junit.Ignore;
import org.junit.Test;

import com.sequoiadb.base.CollectionSpace;
import com.sequoiadb.base.DBCollection;
import com.sequoiadb.base.DBCursor;
import com.sequoiadb.base.Sequoiadb;
import com.sequoiadb.exception.BaseException;
import com.sequoiadb.test.common.*;
/**
 * @author tanzhaobo
 * @brief 测试Decimal公共的函数
 */
public class DecimalCommonTest {

	@BeforeClass
	public static void beforeClass() throws Exception {
	}

	@AfterClass
	public static void afterClass() throws Exception {
	}

	@Before
	public void setUp() throws Exception {
	}

	@After
	public void tearDown() throws Exception {
	}
	
	/*
	 * test DecimalCommon::genBSONDecimal()
	 * */
	@Test
	@Ignore
	public void genBSONDecimalTest() {
		BSONDecimal decimal = DecimalCommon.genBSONDecimal();
		System.out.println("decimal is: " + decimal);
	}
	
	/*
	 * test DecimalCommon::genBSONDecimal(...)
	 * */
	@Test
	@Ignore
	public void genBSONDecimalTest2() {
		BSONDecimal decimal1 = DecimalCommon.genBSONDecimal(true, true, true, 10);
		System.out.println("decimal1 is: " + decimal1);
		
		BSONDecimal decimal2 = DecimalCommon.genBSONDecimal(true, true, false, 0);
		System.out.println("decimal2 is: " + decimal2);
		
		BSONDecimal decimal3 = DecimalCommon.genBSONDecimal(true, false, false, 0);
		System.out.println("decimal3 is: " + decimal3);
		
		BSONDecimal decimal4 = DecimalCommon.genBSONDecimal(false, false, false, 0);
		System.out.println("decimal4 is: " + decimal4);
	}
	
	/*
	 * test DecimalCommon::genIntegerBSONDecimal()
	 * */
	@Test
	@Ignore
	public void genIntegerBSONDecimalTest() {
		BSONDecimal decimal1 = DecimalCommon.genIntegerBSONDecimal(true, true);
		System.out.println("decimal1 is: " + decimal1);
		
		BSONDecimal decimal2 = DecimalCommon.genIntegerBSONDecimal(true, false);
		System.out.println("decimal2 is: " + decimal2);
		
		BSONDecimal decimal3 = DecimalCommon.genIntegerBSONDecimal(false, false);
		System.out.println("decimal3 is: " + decimal3);
	}
	
	

}
