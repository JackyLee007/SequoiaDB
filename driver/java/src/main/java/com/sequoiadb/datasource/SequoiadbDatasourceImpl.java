/**
 * Copyright (C) 2012 SequoiaDB Inc.
 * <p>
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * <p>
 * http://www.apache.org/licenses/LICENSE-2.0
 * <p>
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
/**
 * @package com.sequoiadb.datasource;
 * @brief SequoiaDB Data Source
 * @author tanzhaobo
 */
package com.sequoiadb.datasource;

import com.sequoiadb.base.DBCursor;
import com.sequoiadb.base.Sequoiadb;
import com.sequoiadb.base.SequoiadbConstants;
import com.sequoiadb.exception.BaseException;
import com.sequoiadb.exception.SDBError;
import com.sequoiadb.net.ConfigOptions;
import org.bson.BSONObject;
import org.bson.BasicBSONObject;
import org.bson.types.BasicBSONList;

import java.net.InetAddress;
import java.net.UnknownHostException;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantReadWriteLock;

/**
 * @class SequoiadbDatasourceImpl
 * @brief The implements for SequoiaDB data source
 * @since v1.12.6 & v2.2
 */
public class SequoiadbDatasourceImpl {
    private List<String> _normalAddrs = Collections.synchronizedList(new ArrayList<String>());
    private ConcurrentSkipListSet<String> _abnormalAddrs = new ConcurrentSkipListSet<String>();
    private ConcurrentSkipListSet<String> _localAddrs = new ConcurrentSkipListSet<String>();
    private List<String> _localIPs = Collections.synchronizedList(new ArrayList<String>());
    private LinkedBlockingQueue<Sequoiadb> _destroyConnQueue = new LinkedBlockingQueue<Sequoiadb>();
    private IConnectionPool _idleConnPool = null;
    private IConnectionPool _usedConnPool = null;
    private IConnectStrategy _strategy = null;
    private ConnectionItemMgr _connItemMgr = null;
    private final Object _createConnSignal = new Object();
    private String _username = null;
    private String _password = null;
    private ConfigOptions _nwOpt = null;
    private DatasourceOptions _dsOpt = null;
    private long _currentSequenceNumber = 0;
    private ExecutorService _threadExec = null;
    private ScheduledExecutorService _timerExec = null;
    private volatile boolean _isDatasourceOn = false;
    private volatile boolean _hasClosed = false;
    private ReentrantReadWriteLock _rwLock = new ReentrantReadWriteLock();
    private final Object _objForReleaseConn = new Object();
    private volatile BaseException _lastException;
    private volatile BSONObject _sessionAttr = null;
    private Random _rand = new Random(47);
    private double MULTIPLE = 1.5;
    private volatile int _preDeleteInterval = 0;
    private static final int _deleteInterval = 180000; // 3min
    @SuppressWarnings("unused")
    private final Object finalizerGuardian = new Object() {
        @Override
        protected void finalize() throws Throwable {
            try {
                close();
            } catch (Exception e) {
            }
        }
    };

    class ExitClearUpTask extends Thread {
        public void run() {
            try {
                close();
            } catch (Exception e) {
            }
        }
    }

    class CreateConnectionTask implements Runnable {
        public void run() {
            try {
                while (!Thread.interrupted()) {
                    synchronized (_createConnSignal) {
                        _createConnSignal.wait();
                    }
                    Lock rlock = _rwLock.readLock();
                    rlock.lock();
                    try {
                        if (Thread.interrupted()) {
                            return;
                        } else {
                            _createConnections();
                        }
                    } finally {
                        rlock.unlock();
                    }
                }
            } catch (InterruptedException e) {
            }
        }
    }

    class DestroyConnectionTask implements Runnable {
        public void run() {
            try {
                while (!Thread.interrupted()) {
                    Sequoiadb sdb = _destroyConnQueue.take();
                    try {
                        sdb.disconnect();
                    } catch (BaseException e) {
                        continue;
                    }
                }
            } catch (InterruptedException e) {
                try {
                    Sequoiadb[] arr = (Sequoiadb[]) _destroyConnQueue.toArray();
                    for (Sequoiadb db : arr) {
                        try {
                            db.disconnect();
                        } catch (Exception ex) {
                        }
                    }
                } catch (Exception exp) {
                }
            }
        }
    }

    class CheckConnectionTask implements Runnable {
        @Override
        public void run() {
            Lock wlock = _rwLock.writeLock();
            wlock.lock();
            try {
                if (Thread.interrupted()) {
                    return;
                }
                if (_hasClosed) {
                    return;
                }
                if (!_isDatasourceOn) {
                    return;
                }
                if (_dsOpt.getKeepAliveTimeout() > 0) {
                    long lastTime = 0;
                    long currentTime = System.currentTimeMillis();
                    ConnItem connItem = null;
                    while ((connItem = _strategy.peekConnItemForDeleting()) != null) {
                        Sequoiadb sdb = _idleConnPool.peek(connItem);
                        lastTime = sdb.getConnection().getLastUseTime();
                        if (currentTime - lastTime + _preDeleteInterval >= _dsOpt.getKeepAliveTimeout()) {
                            connItem = _strategy.pollConnItemForDeleting();
                            sdb = _idleConnPool.poll(connItem);
                            try {
                                _destroyConnQueue.add(sdb);
                            } finally {
                                _connItemMgr.releaseItem(connItem);
                            }
                        } else {
                            break;
                        }
                    }
                }
                if (_idleConnPool.count() > _dsOpt.getMaxIdleCount()) {
                    int destroyCount = _idleConnPool.count() - _dsOpt.getMaxIdleCount();
                    _reduceIdleConnections(destroyCount);
                }
            } finally {
                wlock.unlock();
            }
        }
    }

    class RetrieveAddressTask implements Runnable {
        public void run() {
            Lock rlock = _rwLock.readLock();
            rlock.lock();
            try {
                if (Thread.interrupted()) {
                    return;
                }
                if (_hasClosed) {
                    return;
                }
                if (_abnormalAddrs.size() == 0) {
                    return;
                }
                Iterator<String> abnormalAddrSetItr = _abnormalAddrs.iterator();
                ConfigOptions nwOpt = new ConfigOptions();
                String addr = "";
                nwOpt.setConnectTimeout(100); // 100ms
                nwOpt.setMaxAutoConnectRetryTime(0);
                while (abnormalAddrSetItr.hasNext()) {
                    addr = abnormalAddrSetItr.next();
                    try {
                        @SuppressWarnings("unused")
                        Sequoiadb sdb = new Sequoiadb(addr, _username, _password, nwOpt);
                        try {
                            sdb.disconnect();
                        } catch (Exception e) {
                        }
                    } catch (Exception e) {
                        continue;
                    }
                    abnormalAddrSetItr.remove();
                    synchronized (_normalAddrs) {
                        if (!_normalAddrs.contains(addr)) {
                            _normalAddrs.add(addr);
                        }
                    }
                    _strategy.addAddress(addr);
                }
            } finally {
                rlock.unlock();
            }
        }
    }

    class SynchronizeAddressTask implements Runnable {
        @Override
        public void run() {
            Lock wlock = _rwLock.writeLock();
            wlock.lock();
            try {
                if (Thread.interrupted()) {
                    return;
                }
                if (_hasClosed) {
                    return;
                }
                if (_dsOpt.getSyncCoordInterval() == 0) {
                    return;
                }
                Iterator<String> itr = _normalAddrs.iterator();
                Sequoiadb sdb = null;
                while (itr.hasNext()) {
                    String addr = itr.next();
                    try {
                        sdb = new Sequoiadb(addr, _username, _password, _nwOpt);
                        break;
                    } catch (BaseException e) {
                        continue;
                    }
                }
                if (sdb == null) {
                    return;
                }
                List<String> cachedAddrList;
                try {
                    cachedAddrList = _synchronizeCoordAddr(sdb);
                } catch (Exception e) {
                    return;
                } finally {
                    try {
                        sdb.disconnect();
                    } catch (Exception e) {
                    }
                    sdb = null;
                }
                List<String> incList = new ArrayList<String>();
                List<String> decList = new ArrayList<String>();
                String addr = null;
                if (cachedAddrList != null && cachedAddrList.size() > 0) {
                    itr = _normalAddrs.iterator();
                    for (int i = 0; i < 2; i++, itr = _abnormalAddrs.iterator()) {
                        while (itr.hasNext()) {
                            addr = itr.next();
                            if (!cachedAddrList.contains(addr))
                                decList.add(addr);
                        }
                    }
                    itr = cachedAddrList.iterator();
                    while (itr.hasNext()) {
                        addr = itr.next();
                        if (!_normalAddrs.contains(addr) &&
                                !_abnormalAddrs.contains(addr))
                            incList.add(addr);
                    }
                }
                if (incList.size() > 0) {
                    itr = incList.iterator();
                    while (itr.hasNext()) {
                        addr = itr.next();
                        synchronized (_normalAddrs) {
                            if (!_normalAddrs.contains(addr)) {
                                _normalAddrs.add(addr);
                            }
                        }
                        _strategy.addAddress(addr);
                    }
                }
                if (decList.size() > 0) {
                    itr = decList.iterator();
                    while (itr.hasNext()) {
                        addr = itr.next();
                        _normalAddrs.remove(addr);
                        _abnormalAddrs.remove(addr);
                        _removeAddrInStrategy(addr);
                    }
                }
            } finally {
                wlock.unlock();
            }
        }

        private List<String> _synchronizeCoordAddr(Sequoiadb sdb) {
            List<String> addrList = new ArrayList<String>();
            BSONObject condition = new BasicBSONObject();
            condition.put("GroupName", "SYSCoord");
            BSONObject select = new BasicBSONObject();
            select.put("Group.HostName", "");
            select.put("Group.Service", "");
            DBCursor cursor = sdb.getList(Sequoiadb.SDB_LIST_GROUPS, condition, select, null);
            BaseException exp = new BaseException(SDBError.SDB_SYS, "Invalid coord information got from catalog");
            try {
                while (cursor.hasNext()) {
                    BSONObject obj = cursor.getNext();
                    BasicBSONList arr = (BasicBSONList) obj.get("Group");
                    if (arr == null) throw exp;
                    Object[] objArr = arr.toArray();
                    for (int i = 0; i < objArr.length; i++) {
                        BSONObject subObj = (BasicBSONObject) objArr[i];
                        String hostName = (String) subObj.get("HostName");
                        if (hostName == null || hostName.trim().isEmpty()) throw exp;
                        String svcName = "";
                        BasicBSONList subArr = (BasicBSONList) subObj.get("Service");
                        if (subArr == null) throw exp;
                        Object[] subObjArr = subArr.toArray();
                        for (int j = 0; j < subObjArr.length; j++) {
                            BSONObject subSubObj = (BSONObject) subObjArr[j];
                            Integer type = (Integer) subSubObj.get("Type");
                            if (type == null) throw exp;
                            if (type == 0) {
                                svcName = (String) subSubObj.get("Name");
                                if (svcName == null || svcName.trim().isEmpty()) throw exp;
                                String ip;
                                try {
                                    ip = _parseHostName(hostName.trim());
                                } catch (Exception e) {
                                    break;
                                }
                                addrList.add(ip + ":" + svcName.trim());
                                break;
                            }
                        }
                    }
                }
            } finally {
                cursor.close();
            }
            return addrList;
        }
    }

    /**
     * @fn SequoiadbDatasourceImpl(List<String> urls, String username, String password,
     *ConfigOptions nwOpt, DatasourceOptions dsOpt)
     * @brief constructor.
     * @param urls the addresses of coord nodes, can't be null or empty,
     *        e.g."ubuntu1:11810","ubuntu2:11810",...
     * @param username the user name for logging sequoiadb
     * @param password the password for logging sequoiadb
     * @param nwOpt the options for connection
     * @param dsOpt the options for connection pool
     * @note When offer several addresses for connection pool to use, if
     *       some of them are not available(invalid address, network error, coord shutdown,
     *       catalog replica group is not available), we will put these addresses
     *       into a queue, and check them periodically. If some of them is valid again,
     *       get them back for use. When connection pool get a unavailable address to connect,
     *       the default timeout is 100ms, and default retry time is 0. Parameter nwOpt can
     *       can change both of the default value.
     * @see ConfigOptions
     * @see DatasourceOptions
     * @exception com.sequoiadb.exception.BaseException
     */
    public SequoiadbDatasourceImpl(List<String> urls, String username, String password,
                                   ConfigOptions nwOpt, DatasourceOptions dsOpt) throws BaseException {
        if (null == urls || 0 == urls.size())
            throw new BaseException(SDBError.SDB_INVALIDARG, "coord addresses can't be empty or null");

        _init(urls, username, password, nwOpt, dsOpt);
    }

    /**
     * @fn SequoiadbDatasourceImpl(String url, String username, String password,
     *DatasourceOptions dsOpt)
     * @brief Constructor.
     * @param url the address of coord, can't be empty or null, e.g."ubuntu1:11810"
     * @param username the user name for logging sequoiadb
     * @param password the password for logging sequoiadb
     * @param dsOpt the options for connection pool
     * @exception com.sequoiadb.exception.BaseException
     */
    public SequoiadbDatasourceImpl(String url, String username, String password,
                                   DatasourceOptions dsOpt) throws BaseException {
        if (url == null || url.isEmpty())
            throw new BaseException(SDBError.SDB_INVALIDARG, "coord address can't be empty or null");
        ArrayList<String> urls = new ArrayList<String>();
        urls.add(url);
        _init(urls, username, password, null, dsOpt);
    }

    /**
     * @fn int getIdleConnNum()
     * @brief Get the current idle connection amount.
     */
    public int getIdleConnNum() {
        if (_idleConnPool == null)
            return 0;
        else
            return _idleConnPool.count();
    }

    /**
     * @fn int getUsedConnNum()
     * @brief Get the current used connection amount.
     */
    public int getUsedConnNum() {
        if (_usedConnPool == null)
            return 0;
        else
            return _usedConnPool.count();
    }

    /**
     * @fn int getNormalAddrNum()
     * @brief Get the current normal address amount.
     */
    public int getNormalAddrNum() {
        return _normalAddrs.size();
    }
    
    /**
     * @fn int getAbnormalAddrNum()
     * @brief Get the current abnormal address amount.
     */
    public int getAbnormalAddrNum() {
        return _abnormalAddrs.size();
    }

    /**
     * @fn int getLocalAddrNum()
     * @brief Get the amount of local coord node address .
     * @return the amount of local coord node address
     * @note this API works only when the pool is enabled and the connect
     *       strategy is ConnectStrategy.LOCAL,
     *       otherwise, return 0.
     * @exception com.sequoiadb.Exception.BaseException
     * @since v1.12.6 & v2.2
     */
    public int getLocalAddrNum() {
        return _localAddrs.size();
    }

    /**
     * @fn void addCoord(String url)
     * @brief Add coord address.
     * @param url The address in format "192.168.20.168:11810"
     * @exception com.sequoiadb.Exception.BaseException
     */
    public void addCoord(String url) throws BaseException {
        Lock wlock = _rwLock.writeLock();
        wlock.lock();
        try {
            if (_hasClosed) {
                throw new BaseException(SDBError.SDB_SYS, "connection pool has closed");
            }
            if (null == url || "" == url) {
                throw new BaseException(SDBError.SDB_INVALIDARG, "coord address can't be empty or null");
            }
            String addr = _parseCoordAddr(url);
            if (_normalAddrs.contains(addr) ||
                    _abnormalAddrs.contains(addr)) {
                return;
            }
            synchronized (_normalAddrs) {
                if (!_normalAddrs.contains(addr)) {
                    _normalAddrs.add(addr);
                }
            }
            if (ConcreteLocalStrategy.isLocalAddress(addr, _localIPs))
                _localAddrs.add(addr);
            if (_isDatasourceOn) {
                _strategy.addAddress(addr);
            }
        } finally {
            wlock.unlock();
        }
    }

    /**
     * @fn void removeCoord(String url)
     * @brief Remove coord address.
     * @since v1.12.6 & v2.2
     */
    public void removeCoord(String url) throws BaseException {
        Lock wlock = _rwLock.writeLock();
        wlock.lock();
        try {
            if (_hasClosed) {
                throw new BaseException(SDBError.SDB_SYS, "connection pool has closed");
            }
            if (null == url) {
                throw new BaseException(SDBError.SDB_INVALIDARG, "coord address can't be null");
            }
            String addr = _parseCoordAddr(url);
            _normalAddrs.remove(addr);
            _abnormalAddrs.remove(addr);
            _localAddrs.remove(addr);
            if (_isDatasourceOn) {
                _removeAddrInStrategy(addr);
            }
        } finally {
            wlock.unlock();
        }
    }

    /**
     * @fn DatasourceOptions getDatasourceOptions()
     * @brief Get a copy of the connection pool options
     * @return a copy of the connection pool options
     * @throws BaseException
     * @since v1.12.6 & v2.2
     */
    public DatasourceOptions getDatasourceOptions() throws BaseException {
        Lock rlock = _rwLock.readLock();
        rlock.lock();
        try {
            return (DatasourceOptions) _dsOpt.clone();
        } catch (CloneNotSupportedException e) {
            throw new BaseException(SDBError.SDB_SYS, "failed to clone connnection pool options");
        } finally {
            rlock.unlock();
        }
    }

    /**
     * @fn void updateDatasourceOptions(DatasourceOptions dsOpt)
     * @brief Update connection pool options.
     * @return dsOpt the newly connection pool for update
     * @exception com.sequoiadb.Exception.BaseException
     * @since v1.12.6 & v2.2
     */
    public void updateDatasourceOptions(DatasourceOptions dsOpt) throws BaseException {
        Lock wlock = _rwLock.writeLock();
        wlock.lock();
        try {
            if (_hasClosed) {
                throw new BaseException(SDBError.SDB_SYS, "connection pool has closed");
            }
            _checkDatasourceOptions(dsOpt);
            int previousMaxCount = _dsOpt.getMaxCount();
            int previousCheckInterval = _dsOpt.getCheckInterval();
            int previousSyncCoordInterval = _dsOpt.getSyncCoordInterval();
            ConnectStrategy previousStrategy = _dsOpt.getConnectStrategy();

            try {
                _dsOpt = (DatasourceOptions) dsOpt.clone();
            } catch (CloneNotSupportedException e) {
                throw new BaseException(SDBError.SDB_INVALIDARG, "failed to clone connection pool options");
            }
            if (!_isDatasourceOn) {
                return;
            }
            if (_dsOpt.getMaxCount() == 0) {
                disableDatasource();
                return;
            }
            _preDeleteInterval = (int) (_dsOpt.getCheckInterval() * MULTIPLE);
            if (previousMaxCount != _dsOpt.getMaxCount()) {
                if (_connItemMgr != null) {
                    _connItemMgr.resetCapacity(_dsOpt.getMaxCount());
                    if (_dsOpt.getMaxCount() < previousMaxCount) {
                        int deltaNum = getIdleConnNum() + getUsedConnNum() - _dsOpt.getMaxCount();
                        int destroyNum = (deltaNum > getIdleConnNum()) ? getIdleConnNum() : deltaNum;
                        if (destroyNum > 0)
                            _reduceIdleConnections(destroyNum);
                        _currentSequenceNumber = _connItemMgr.getCurrentSequenceNumber();
                    }
                } else {
                    throw new BaseException(SDBError.SDB_SYS, "the item manager is null");
                }
            }
            if (previousStrategy != _dsOpt.getConnectStrategy()) {
                _cancelTimer();
                _cancelThreads();
                _changeStrategy();
                _startTimer();
                _startThreads();
                _currentSequenceNumber = _connItemMgr.getCurrentSequenceNumber();
            } else if (previousCheckInterval != _dsOpt.getCheckInterval() ||
                    previousSyncCoordInterval != _dsOpt.getSyncCoordInterval()) {
                _cancelTimer();
                _startTimer();
            }
        } finally {
            wlock.unlock();
        }
    }

    /**
     * @fn void enableDatasource()
     * @brief Enable data source.
     * @note When maxCount is 0, set it to be the default value(500).
     * @return void
     * @exception com.sequoiadb.Exception.BaseException
     * @exception InterruptedException
     * @since v1.12.6 & v2.2
     */
    public void enableDatasource() {
        Lock wlock = _rwLock.writeLock();
        wlock.lock();
        try {
            if (_hasClosed) {
                throw new BaseException(SDBError.SDB_SYS, "connection pool has closed");
            }
            if (_isDatasourceOn) {
                return;
            }
            if (_dsOpt.getMaxCount() == 0) {
                _dsOpt.setMaxCount(500);
            }
            _enableDatasource(_dsOpt.getConnectStrategy());
        } finally {
            wlock.unlock();
        }
        return;
    }

    /**
     * @fn void disableDatasource()
     * @brief Disable data source.
     * @return void
     * @exception com.sequoiadb.Exception.BaseException
     * @exception InterruptedException
     * @note After disable data source, the pool will not manage
     *       the connections again. When a getting request comes,
     *       the pool build and return a connection; When a connection
     *       is put back, the pool disconnect it directly.
     * @since v1.12.6 & v2.2
     */
    public void disableDatasource() {
        Lock wlock = _rwLock.writeLock();
        wlock.lock();
        try {
            if (_hasClosed) {
                throw new BaseException(SDBError.SDB_SYS, "connection pool has closed");
            }
            if (!_isDatasourceOn) {
                return;
            }
            _cancelTimer();
            _cancelThreads();
            _closePoolConnections(_idleConnPool);
            _isDatasourceOn = false;
        } finally {
            wlock.unlock();
        }
        return;
    }

    /**
     * @fn Sequoiadb getConnection()
     * @brief Get a connection from current connection pool.
     * @param timeout the time for waiting for connection in millisecond. 0 for waiting until a connection is available.
     * @return Sequoiadb the connection for using
     * @exception com.sequoiadb.Exception.BaseException
     * @exception InterruptedException Actually, nothing happen. Throw this for compatibility reason.
     * @note When the pool runs out, a request will wait up to 5 seconds. When time is up, if the pool
     * 		 still has no idle connection, it throws BaseException with the type of "SDB_DRIVER_DS_RUNOUT".
     */
    public Sequoiadb getConnection() throws BaseException, InterruptedException {
        return getConnection(5000);
    }

    /**
     * @fn Sequoiadb getConnection(long timeout)
     * @brief Get a connection from current connection pool.
     * @param timeout the time for waiting for connection in millisecond. 0 for waiting until a connection is available.
     * @return Sequoiadb the connection for using
     * @exception com.sequoiadb.Exception.BaseException
     *            when connection pool run out, throws BaseException with the type of "SDB_DRIVER_DS_RUNOUT"
     * @exception InterruptedException Actually, nothing happen. Throw this for compatibility reason.
     * @since v1.12.6 & v2.2
     */
    public Sequoiadb getConnection(long timeout) throws BaseException, InterruptedException {
        if (timeout < 0) {
            throw new BaseException(SDBError.SDB_INVALIDARG, "timeout should >= 0");
        }
        Lock rlock = _rwLock.readLock();
        rlock.lock();
        try {
            Sequoiadb sdb;
            ConnItem connItem;
            long restTime = timeout;
            while (true) {
                sdb = null;
                connItem = null;
                if (_hasClosed) {
                    throw new BaseException(SDBError.SDB_SYS, "connection pool has closed");
                }
                if (!_isDatasourceOn) {
                    return _newConnByNormalAddr();
                }
                if ((connItem = _strategy.pollConnItemForGetting()) != null) {
                    sdb = _idleConnPool.poll(connItem);
                    if (sdb == null) {
                        _connItemMgr.releaseItem(connItem);
                        connItem = null;
                        throw new BaseException(SDBError.SDB_SYS, "no matching connection");
                    }
                } else if ((connItem = _connItemMgr.getItem()) != null) {
                    try {
                        sdb = _newConnByNormalAddr();
                    } catch (BaseException e) {
                        _connItemMgr.releaseItem(connItem);
                        connItem = null;
                        throw e;
                    }
                    if (sdb == null) {
                        _connItemMgr.releaseItem(connItem);
                        connItem = null;
                        throw new BaseException(SDBError.SDB_SYS, "create connection directly failed");
                    } else if (_sessionAttr != null) {
                        try {
                            sdb.setSessionAttr(_sessionAttr);
                        } catch (Exception e) {
                            _connItemMgr.releaseItem(connItem);
                            connItem = null;
                            _destroyConnQueue.add(sdb);
                            throw new BaseException(SDBError.SDB_SYS,
                                    String.format("failed to set the session attribute[%s]",
                                            _sessionAttr.toString()), e);
                        }
                    }
                    connItem.setAddr(sdb.getServerAddress().toString());
                    synchronized (_createConnSignal) {
                        _createConnSignal.notify();
                    }
                } else {
                    long beginTime = 0;
                    long endTime = 0;
                    rlock.unlock();
                    try {
                        if (timeout != 0) {
                            if (restTime <= 0) {
                                break;
                            }
                            beginTime = System.currentTimeMillis();
                            try {
                                synchronized (this) {
                                    this.wait(restTime);
                                }
                            } finally {
                                endTime = System.currentTimeMillis();
                                restTime -= (endTime - beginTime);
                            }
                            continue;
                        } else {
                            try {
                                synchronized (this) {
                                    this.wait(5000);
                                }
                            } finally {
                                continue;
                            }
                        }
                    } finally {
                        rlock.lock();
                    }
                }
                if (sdb.isClosed() ||
                        (_dsOpt.getValidateConnection() && !sdb.isValid())) {
                    _connItemMgr.releaseItem(connItem);
                    connItem = null;
                    _destroyConnQueue.add(sdb);
                    sdb = null;
                    continue;
                } else {
                    break;
                }
            } // while(true)
            if (connItem == null) {
                String detail = _getDataSourceSnapshot();
                if (getNormalAddrNum() == 0 && getUsedConnNum() < _dsOpt.getMaxCount()) {
                    BaseException exception = _getLastException();
                    String errMsg = "get connection failed, no available address for connection, " + detail;
                    if (exception != null) {
                        throw new BaseException(SDBError.SDB_NETWORK, errMsg, exception);
                    } else {
                        throw new BaseException(SDBError.SDB_NETWORK, errMsg);
                    }
                } else {
                    throw new BaseException(SDBError.SDB_DRIVER_DS_RUNOUT,
                            "the pool has run out of connections, " + detail);
                }
            } else {
                _usedConnPool.insert(connItem, sdb);
                _strategy.updateUsedConnItemCount(connItem, 1);
                return sdb;
            }
        } finally {
            rlock.unlock();
        }
    }

    /**
     * @fn void releaseConnection(Sequoiadb sdb)
     * @brief Put the connection back to the connection pool.
     * @param sdb the connection to come back, can't be null
     * @note When the data source is enable, we can't double release
     *       one connection, and we can't offer a connection which is
     *       not belong to the pool.
     * @exception com.sequoiadb.Exception.BaseException
     * @since v1.12.6 & v2.2
     */
    public void releaseConnection(Sequoiadb sdb) throws BaseException {
        Lock rlock = _rwLock.readLock();
        rlock.lock();
        try {
            if (sdb == null) {
                throw new BaseException(SDBError.SDB_INVALIDARG, "connection can't be null");
            }
            if (_hasClosed) {
                throw new BaseException(SDBError.SDB_SYS, "connection pool has closed");
            }
            if (!_isDatasourceOn) {
                synchronized (_objForReleaseConn) {
                    if (_usedConnPool != null && _usedConnPool.contains(sdb)) {
                        ConnItem item = _usedConnPool.poll(sdb);
                        if (item == null) {
                            throw new BaseException(SDBError.SDB_SYS,
                                    "the pool does't have item for the coming back connection");
                        }
                        _connItemMgr.releaseItem(item);
                    }
                }
                try {
                    sdb.disconnect();
                } catch (Exception e) {
                }
                return;
            }
            ConnItem item = null;
            synchronized (_objForReleaseConn) {
                if (_usedConnPool.contains(sdb)) {
                    item = _usedConnPool.poll(sdb);
                    if (item == null) {
                        throw new BaseException(SDBError.SDB_SYS,
                                "the pool does not have item for the coming back connection");
                    }
                } else {
                    throw new BaseException(SDBError.SDB_INVALIDARG,
                            "the connection pool doesn't contain the offered connection");
                }
            }
            _strategy.updateUsedConnItemCount(item, -1);
            if (_connIsValid(item, sdb)) {
                _idleConnPool.insert(item, sdb);
                _strategy.addConnItemAfterReleasing(item);
                synchronized (this) {
                    notifyAll();
                }
            } else {
                _connItemMgr.releaseItem(item);
                _destroyConnQueue.add(sdb);
                synchronized (this) {
                    notifyAll();
                }
            }
        } finally {
            rlock.unlock();
        }
    }

    /**
     * @fn void close(Sequoiadb sdb)
     * @brief Put the connection back to the connection pool.
     * @param sdb the connection to come back, can't be null
     * @note When the data source is enable, we can't double release
     *       one connection, and we can't offer a connection which is
     *       not belong to the pool.
     * @exception com.sequoiadb.Exception.BaseException
     * @deprecated
     * @see releaseConnection, use releaseConnection instead
     */
    public void close(Sequoiadb sdb) throws BaseException {
        releaseConnection(sdb);
    }

    /**
     * @fn void close()
     * @brief clean all resources of current connection pool
     */
    public void close() {
        Lock wlock = _rwLock.writeLock();
        wlock.lock();
        try {
            if (_hasClosed) {
                return;
            }
            if (_isDatasourceOn) {
                _cancelTimer();
                _cancelThreads();
            }
            if (_idleConnPool != null)
                _closePoolConnections(_idleConnPool);
            if (_usedConnPool != null)
                _closePoolConnections(_usedConnPool);
            _isDatasourceOn = false;
            _hasClosed = true;
        } finally {
            wlock.unlock();
        }
    }

    private void _init(List<String> addrList, String username, String password,
                       ConfigOptions nwOpt, DatasourceOptions dsOpt) throws BaseException {
        for (String elem : addrList) {
            if (elem != null && !elem.isEmpty()) {
                String addr = _parseCoordAddr(elem);
                synchronized (_normalAddrs) {
                    if (!_normalAddrs.contains(addr)) {
                        _normalAddrs.add(addr);
                    }
                }
            }
        }
        _username = (username == null) ? "" : username;
        _password = (password == null) ? "" : password;
        if (nwOpt == null) {
            ConfigOptions temp = new ConfigOptions();
            temp.setConnectTimeout(100);
            temp.setMaxAutoConnectRetryTime(0);
            _nwOpt = temp;
        } else {
            _nwOpt = nwOpt;
        }
        if (dsOpt == null) {
            _dsOpt = new DatasourceOptions();
        } else {
            try {
                _dsOpt = (DatasourceOptions) dsOpt.clone();
            } catch (CloneNotSupportedException e) {
                throw new BaseException(SDBError.SDB_INVALIDARG, "failed to clone connection pool options");
            }
        }

        List<String> localIPList = ConcreteLocalStrategy.getNetCardIPs();
        _localIPs.addAll(localIPList);
        List<String> localCoordList =
                ConcreteLocalStrategy.getLocalCoordIPs(_normalAddrs, localIPList);
        _localAddrs.addAll(localCoordList);

        _checkDatasourceOptions(_dsOpt);

        if (_dsOpt.getMaxCount() == 0) {
            _isDatasourceOn = false;
        } else {
            _enableDatasource(_dsOpt.getConnectStrategy());
        }
        Runtime.getRuntime().addShutdownHook(new ExitClearUpTask());
    }

    private void _startTimer() {
        _timerExec = Executors.newScheduledThreadPool(1,
                new ThreadFactory() {
                    public Thread newThread(Runnable r) {
                        Thread t = Executors.defaultThreadFactory().newThread(r);
                        t.setDaemon(true);
                        return t;
                    }
                }
        );
        if (_dsOpt.getSyncCoordInterval() > 0) {
            _timerExec.scheduleAtFixedRate(new SynchronizeAddressTask(), 0, _dsOpt.getSyncCoordInterval(),
                    TimeUnit.MILLISECONDS);
        }
        _timerExec.scheduleAtFixedRate(new CheckConnectionTask(), _dsOpt.getCheckInterval(),
                _dsOpt.getCheckInterval(), TimeUnit.MILLISECONDS);
        _timerExec.scheduleAtFixedRate(new RetrieveAddressTask(), 60, 60, TimeUnit.SECONDS);
    }

    private void _cancelTimer() {
        _timerExec.shutdownNow();
    }

    private void _startThreads() {
        _threadExec = Executors.newCachedThreadPool(
                new ThreadFactory() {
                    public Thread newThread(Runnable r) {
                        Thread t = Executors.defaultThreadFactory().newThread(r);
                        t.setDaemon(true);
                        return t;
                    }
                }
        );
        _threadExec.execute(new CreateConnectionTask());
        _threadExec.execute(new DestroyConnectionTask());
        _threadExec.shutdown();
    }

    private void _cancelThreads() {
        _threadExec.shutdownNow();
    }

    private void _changeStrategy() {
        List<Pair> idleConnPairs = new ArrayList<Pair>();
        List<Pair> usedConnPairs = new ArrayList<Pair>();
        Iterator<Pair> itr = null;
        itr = _idleConnPool.getIterator();
        while (itr.hasNext()) {
            idleConnPairs.add(itr.next());
        }
        itr = _usedConnPool.getIterator();
        while (itr.hasNext()) {
            usedConnPairs.add(itr.next());
        }
        _strategy = _createStrategy(_dsOpt.getConnectStrategy());
        _strategy.init(_normalAddrs, idleConnPairs, usedConnPairs);
    }

    private void _closePoolConnections(IConnectionPool pool) {
        if (pool == null) {
            return;
        }
        Iterator<Pair> iter = pool.getIterator();
        while (iter.hasNext()) {
            Pair pair = iter.next();
            Sequoiadb sdb = pair.second();
            try {
                sdb.disconnect();
            } catch (Exception e) {
            }
        }
        List<ConnItem> list = pool.clear();
        for (ConnItem item : list)
            _connItemMgr.releaseItem(item);
    }

    private void _checkDatasourceOptions(DatasourceOptions newOpt) throws BaseException {
        if (newOpt == null) {
            throw new BaseException(SDBError.SDB_INVALIDARG, "the offering datasource options can't be null");
        }

        int deltaIncCount = newOpt.getDeltaIncCount();
        int maxIdleCount = newOpt.getMaxIdleCount();
        int maxCount = newOpt.getMaxCount();
        int keepAliveTimeout = newOpt.getKeepAliveTimeout();
        int checkInterval = newOpt.getCheckInterval();
        int syncCoordInterval = newOpt.getSyncCoordInterval();
        List<Object> preferredInstanceList = newOpt.getPreferedInstanceObjects();
        String preferredInstanceMode = newOpt.getPreferedInstanceMode();
        int sessionTimeout = newOpt.getSessionTimeout();

        if (maxCount < 0)
            throw new BaseException(SDBError.SDB_INVALIDARG, "maxCount can't be less then 0");

        if (deltaIncCount <= 0)
            throw new BaseException(SDBError.SDB_INVALIDARG, "deltaIncCount should be more then 0");

        if (maxIdleCount < 0)
            throw new BaseException(SDBError.SDB_INVALIDARG, "maxIdleCount can't be less then 0");

        if (keepAliveTimeout < 0)
            throw new BaseException(SDBError.SDB_INVALIDARG, "keepAliveTimeout can't be less than 0");

        if (checkInterval <= 0)
            throw new BaseException(SDBError.SDB_INVALIDARG, "checkInterval should be more than 0");
        if (0 != keepAliveTimeout && checkInterval >= keepAliveTimeout)
            throw new BaseException(SDBError.SDB_INVALIDARG, "when keepAliveTimeout is not 0, checkInterval should be less than keepAliveTimeout");

        if (syncCoordInterval < 0)
            throw new BaseException(SDBError.SDB_INVALIDARG, "syncCoordInterval can't be less than 0");

        if (maxCount != 0) {
            if (deltaIncCount > maxCount)
                throw new BaseException(SDBError.SDB_INVALIDARG, "deltaIncCount can't be great then maxCount");
            if (maxIdleCount > maxCount)
                throw new BaseException(SDBError.SDB_INVALIDARG, "maxIdleCount can't be great then maxCount");
        }

        if (preferredInstanceList != null && preferredInstanceList.size() > 0) {
            for (Object obj : preferredInstanceList) {
                if (obj instanceof String) {
                    String s = (String) obj;
                    if (!"M".equals(s) && !"m".equals(s) &&
                            !"S".equals(s) && !"s".equals(s) &&
                            !"A".equals(s) && !"a".equals(s)) {
                        throw new BaseException(SDBError.SDB_INVALIDARG,
                                "the element of preferred instance should be 'M'/'S'/'A'/'m'/'s'/'a/['1','255'], but it is "
                                        + s);
                    }
                } else if (obj instanceof Integer) {
                    int i = (Integer) obj;
                    if (i <= 0 || i > 255) {
                        throw new BaseException(SDBError.SDB_INVALIDARG,
                                "the element of preferred instance should be 'M'/'S'/'A'/'m'/'s'/'a/['1','255'], but it is "
                                        + i);
                    }
                } else {
                    throw new BaseException(SDBError.SDB_INVALIDARG,
                            "the preferred instance should instance of int or String, but it is "
                                    + (obj == null ? null : obj.getClass()));
                }
            }
            if (!SequoiadbConstants.PREFERED_INSTANCE_MODE_ORDERED.equals(preferredInstanceMode) &&
                    !SequoiadbConstants.PREFERED_INSTANCE_MODE_RANDON.equals(preferredInstanceMode)) {
                throw new BaseException(SDBError.SDB_INVALIDARG,
                        String.format("the preferred instance mode should be '%s' or '%s', but it is %s",
                                SequoiadbConstants.PREFERED_INSTANCE_MODE_ORDERED,
                                SequoiadbConstants.PREFERED_INSTANCE_MODE_RANDON,
                                preferredInstanceMode));
            }
            if (sessionTimeout < -1) {
                throw new BaseException(SDBError.SDB_INVALIDARG,
                        "the session timeout can not less than -1");
            }
            _sessionAttr = newOpt.getSessionAttr();
        }
    }

    private Sequoiadb _newConnByNormalAddr() throws BaseException {
        Sequoiadb sdb = null;
        String address = null;
        try {
            while (true) {
                if (_isDatasourceOn) {
                    address = _strategy.getAddress();
                } else {
                    synchronized (_normalAddrs) {
                        int size = _normalAddrs.size();
                        if (size > 0) {
                            address = _normalAddrs.get(_rand.nextInt(size));
                        }
                    }
                }
                if (address != null) {
                    try {
                        sdb = new Sequoiadb(address, _username, _password, _nwOpt);
                        break;
                    } catch (BaseException e) {
                        _setLastException(e);
                        String errType = e.getErrorType();
                        if (errType.equals("SDB_NETWORK") || errType.equals("SDB_INVALIDARG") ||
                                errType.equals("SDB_NET_CANNOT_CONNECT")) {
                            _handleErrorAddr(address);
                            continue;
                        } else {
                            throw e;
                        }
                    }
                } else {
                    sdb = _newConnByAbnormalAddr();
                    break;
                }
            }

            if (sdb == null) {
                throw new BaseException(SDBError.SDB_SYS, "failed to create connection directly");
            }
        } catch (BaseException e) {
            throw e;
        } catch (Exception e) {
            throw new BaseException(SDBError.SDB_SYS, e);
        }
        return sdb;
    }

    private Sequoiadb _newConnByAbnormalAddr() throws BaseException {
        Sequoiadb retConn = null;
        int retry = 3;
        while (retry-- > 0) {
            Iterator<String> itr = _abnormalAddrs.iterator();
            while (itr.hasNext()) {
                String addr = itr.next();
                try {
                    retConn = new Sequoiadb(addr, _username, _password, _nwOpt);
                } catch (Exception e) {
                    if (e instanceof BaseException) {
                        _setLastException((BaseException) e);
                    }
                    continue;
                }
                _abnormalAddrs.remove(addr);
                synchronized (_normalAddrs) {
                    if (!_normalAddrs.contains(addr)) {
                        _normalAddrs.add(addr);
                    }
                }
                if (_isDatasourceOn) {
                    _strategy.addAddress(addr);
                }
                break;
            }
            if (retConn != null) {
                break;
            }
        }
        if (retConn == null) {
            String detail = _getDataSourceSnapshot();
            BaseException exp = _getLastException();
            String errMsg = "no available address for connection, " + detail;
            if (exp != null) {
                throw new BaseException(SDBError.SDB_NETWORK, errMsg, exp);
            } else {
                throw new BaseException(SDBError.SDB_NETWORK, errMsg);
            }
        }
        return retConn;
    }

    private String _getDataSourceSnapshot() {
        String snapshot = String.format("[thread id: %d], total item: %d, idle item: %d, used item: %d, " +
                        "idle connections: %d, used connections: %d, " +
                        "normal addresses: %d, abnormal addresses: %d, local addresses: %d",
                Thread.currentThread().getId(),
                _connItemMgr.getCapacity(), _connItemMgr.getIdleItemNum(), _connItemMgr.getUsedItemNum(),
                _idleConnPool != null ? _idleConnPool.count() : null,
                _usedConnPool != null ? _usedConnPool.count() : null,
                getNormalAddrNum(), getAbnormalAddrNum(), getLocalAddrNum());
        return snapshot;
    }

    private void _setLastException(BaseException e) {
        _lastException = e;
    }

    private BaseException _getLastException() {
        BaseException exp = _lastException;
        return exp;
    }

    private void _handleErrorAddr(String addr) {
        _normalAddrs.remove(addr);
        _abnormalAddrs.add(addr);
        if (_isDatasourceOn) {
            _removeAddrInStrategy(addr);
        }
    }

    private void _removeAddrInStrategy(String addr) {
        List<ConnItem> list = _strategy.removeAddress(addr);
        Iterator<ConnItem> itr = list.iterator();
        while (itr.hasNext()) {
            ConnItem item = itr.next();
            Sequoiadb sdb = _idleConnPool.poll(item);
            _destroyConnQueue.add(sdb);
            item.setAddr("");
            _connItemMgr.releaseItem(item);
        }
    }

    private void _createConnections() {
        int count = _dsOpt.getDeltaIncCount();
        while (count > 0) {
            Sequoiadb sdb = null;
            String addr = null;
            ConnItem connItem = _connItemMgr.getItem();
            if (connItem == null) {
                break;
            }
            while (true) {
                addr = null;
                addr = _strategy.getAddress();
                if (addr == null) {
                    break;
                }
                try {
                    sdb = new Sequoiadb(addr, _username, _password, _nwOpt);
                    break;
                } catch (BaseException e) {
                    _setLastException(e);
                    String errType = e.getErrorType();
                    if (errType.equals("SDB_NETWORK") || errType.equals("SDB_INVALIDARG") ||
                            errType.equals("SDB_NET_CANNOT_CONNECT")) {
                        _handleErrorAddr(addr);
                        continue;
                    } else {
                        break;
                    }
                } catch (Exception e) {
                    break;
                }
            }
            if (sdb == null) {
                _connItemMgr.releaseItem(connItem);
                break;
            } else if (_sessionAttr != null) {
                try {
                    sdb.setSessionAttr(_sessionAttr);
                } catch (Exception e) {
                    _connItemMgr.releaseItem(connItem);
                    connItem = null;
                    _destroyConnQueue.add(sdb);
                    break;
                }
            }
            connItem.setAddr(addr);
            _idleConnPool.insert(connItem, sdb);
            _strategy.addConnItemAfterCreating(connItem);
            count--;
        }
    }

    private boolean _connIsValid(ConnItem item, Sequoiadb sdb) {
        try {
            sdb.releaseResource();
        } catch (Exception e) {
            try {
                sdb.disconnect();
            } catch (Exception ex) {
            }
            return false;
        }

        if (0 != _dsOpt.getKeepAliveTimeout()) {
            long lastTime = sdb.getConnection().getLastUseTime();
            long currentTime = System.currentTimeMillis();
            if (currentTime - lastTime + _preDeleteInterval >= _dsOpt.getKeepAliveTimeout())
                return false;
        }
        if (item.getSequenceNumber() <= _currentSequenceNumber) {
            return false;
        }
        return true;
    }

    private IConnectStrategy _createStrategy(ConnectStrategy strategy) {
        IConnectStrategy obj = null;
        switch (strategy) {
            case BALANCE:
                obj = new ConcreteBalanceStrategy();
                break;
            case SERIAL:
                obj = new ConcreteSerialStrategy();
                break;
            case RANDOM:
                obj = new ConcreteRandomStrategy();
                break;
            case LOCAL:
                obj = new ConcreteLocalStrategy();
                break;
            default:
                break;
        }
        return obj;
    }

    private void _enableDatasource(ConnectStrategy strategy) {
        _preDeleteInterval = (int) (_dsOpt.getCheckInterval() * MULTIPLE);
        _idleConnPool = new IdleConnectionPool();
        if (_usedConnPool == null) {
            _usedConnPool = new UsedConnectionPool();
            _connItemMgr = new ConnectionItemMgr(_dsOpt.getMaxCount(), null);
        } else {
            List<ConnItem> list = new ArrayList<ConnItem>();
            Iterator<Pair> itr = _usedConnPool.getIterator();
            while (itr.hasNext()) {
                list.add(itr.next().first());
            }
            _connItemMgr = new ConnectionItemMgr(_dsOpt.getMaxCount(), list);
            _currentSequenceNumber = _connItemMgr.getCurrentSequenceNumber();
        }
        _strategy = _createStrategy(strategy);
        _strategy.init(_normalAddrs, null, null);
        _startTimer();
        _startThreads();
        _isDatasourceOn = true;
    }

    private void _reduceIdleConnections(int count) {
        ConnItem connItem = null;
        long lastTime = 0;
        long currentTime = System.currentTimeMillis();
        while (count-- > 0 && (connItem = _strategy.peekConnItemForDeleting()) != null) {
            Sequoiadb sdb = _idleConnPool.peek(connItem);
            lastTime = sdb.getConnection().getLastUseTime();
            if (currentTime - lastTime >= _deleteInterval) {
                connItem = _strategy.pollConnItemForDeleting();
                sdb = _idleConnPool.poll(connItem);
                try {
                    _destroyConnQueue.add(sdb);
                } finally {
                    _connItemMgr.releaseItem(connItem);
                }
            } else {
                break;
            }
        }
    }

    private void _reduceIdleConnections_bak(int count) {
        while (count-- > 0) {
            ConnItem item = _strategy.pollConnItemForDeleting();
            if (item == null) {
                break;
            }
            try {
                Sequoiadb sdb = _idleConnPool.poll(item);
                _destroyConnQueue.add(sdb);
            } finally {
                _connItemMgr.releaseItem(item);
            }
        }
    }

    private String _parseHostName(String hostName) {
        InetAddress ia = null;
        try {
            ia = InetAddress.getByName(hostName);
        } catch (UnknownHostException e) {
            throw new BaseException(SDBError.SDB_SYS, "Failed to parse host name to ip for UnknownHostException", e);
        } catch (SecurityException e) {
            throw new BaseException(SDBError.SDB_SYS, "Failed to parse host name to ip for SecurityException", e);
        }
        return ia.getHostAddress();
    }

    private String _parseCoordAddr(String coordAddr) {
        String retCoordAddr = null;
        if (coordAddr.indexOf(":") > 0) {
            String host = "";
            int port = 0;
            String[] tmp = coordAddr.split(":");
            if (tmp.length < 2)
                throw new BaseException(SDBError.SDB_INVALIDARG, "Point 1: invalid format coord address: " + coordAddr);
            host = tmp[0].trim();
            try {
                host = InetAddress.getByName(host).toString().split("/")[1];
            } catch (Exception e) {
                throw new BaseException(SDBError.SDB_INVALIDARG, e);
            }
            port = Integer.parseInt(tmp[1].trim());
            retCoordAddr = host + ":" + port;
        } else {
            throw new BaseException(SDBError.SDB_INVALIDARG, "Point 2: invalid format coord address: " + coordAddr);
        }
        return retCoordAddr;
    }

}

