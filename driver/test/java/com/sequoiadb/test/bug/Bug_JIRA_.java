package com.sequoiadb.test.bug;

import static org.junit.Assert.*;


import org.bson.BSONObject;
import org.bson.BasicBSONObject;
import org.junit.After;
import org.junit.AfterClass;
import org.junit.Assert;
import org.junit.Before;
import org.junit.BeforeClass;
import org.junit.Test;

import com.sequoiadb.base.CollectionSpace;
import com.sequoiadb.base.DBCollection;
import com.sequoiadb.base.DBCursor;
import com.sequoiadb.base.Sequoiadb;
import com.sequoiadb.exception.BaseException;
import com.sequoiadb.exception.SDBError;
import com.sequoiadb.test.common.*;


public class Bug_JIRA_ {
	private static Sequoiadb sdb ;
	private static CollectionSpace cs ;
	private static DBCollection cl ;
	
	@BeforeClass
	public static void setConnBeforeClass() throws Exception{
		sdb = new Sequoiadb(Constants.COOR_NODE_CONN,"","");
		if(sdb.isCollectionSpaceExist(Constants.TEST_CS_NAME_1)){
			sdb.dropCollectionSpace(Constants.TEST_CS_NAME_1);
			cs = sdb.createCollectionSpace(Constants.TEST_CS_NAME_1);
		}
		else
			cs = sdb.createCollectionSpace(Constants.TEST_CS_NAME_1);
		BSONObject conf = new BasicBSONObject();
		conf.put("ReplSize", 0);
		cl = cs.createCollection(Constants.TEST_CL_NAME_1, conf);
	}
	
	@AfterClass
	public static void DropConnAfterClass() throws Exception {
		sdb.dropCollectionSpace(Constants.TEST_CS_NAME_1);
		sdb.disconnect();
	}
 
	@Before
	public void setUp() throws Exception {
	}

	@After
	public void tearDown() throws Exception {
		cl.delete("");
	}
	
	@Test
	public void jira_2100(){
		Sequoiadb mydb = new Sequoiadb(Constants.COOR_NODE_CONN,"","");
		DBCollection mycl = 
				mydb.getCollectionSpace(Constants.TEST_CS_NAME_1).getCollection(Constants.TEST_CL_NAME_1); 
		DBCursor cur = mycl.query();
		cur.close();
		mydb.disconnect();
		try {
			mydb.closeAllCursors();
			Assert.fail();
		} catch(BaseException e) {
			Assert.assertEquals(SDBError.SDB_NOT_CONNECTED.getErrorCode(), e.getErrorCode());
		}
	}
	
	
}
