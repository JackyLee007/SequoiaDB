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
 * @package com.sequoiadb.base;
 * @brief this option of SequoiadbDatasource
 * @author gaosj
 */
/**
 * @package com.sequoiadb.base;
 * @brief this option of SequoiadbDatasource
 * @author gaosj
 */
package com.sequoiadb.datasource;


import com.sequoiadb.base.SequoiadbConstants;
import com.sequoiadb.exception.BaseException;
import com.sequoiadb.exception.SDBError;
import org.bson.BSONObject;
import org.bson.BasicBSONObject;
import org.bson.types.BasicBSONList;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

import static com.sequoiadb.base.SequoiadbConstants.*;


/**
 * @class DatasourceOptions
 * @brief the options of data source
 * @since v1.12.6 & v2.2
 */
public class DatasourceOptions implements Cloneable {

    private static final List<String> MODE = Arrays.asList("M", "m", "S", "s", "A", "a");
    private static final String DEFAULT_PREFERRD_INSTANCE_MODE = SequoiadbConstants.PREFERED_INSTANCE_MODE_RANDON;
    private static final int DEFAULT_SESSION_TIMEOUT = -1;
    private int _deltaIncCount = 10;
    private int _maxIdleCount = 10;
    private int _maxCount = 500;
    private int _keepAliveTimeout = 0 * 60 * 1000; // 0 min
    private int _checkInterval = 1 * 60 * 1000; // 1 min
    private int _syncCoordInterval = 0; // 0 min
    private boolean _validateConnection = false;
    private ConnectStrategy _connectStrategy = ConnectStrategy.SERIAL;
    private List<Object> _preferedInstance = null;
    private String _preferedInstanceMode = DEFAULT_PREFERRD_INSTANCE_MODE; // "random" or "ordered"
    private int _sessionTimeout = DEFAULT_SESSION_TIMEOUT;

    /**
     * @fn Object clone()
     * @brief Close the current options.
     * @since v1.12.6 and v2.2
     */
    public Object clone() throws CloneNotSupportedException {
        return super.clone();
    }

    /**
     * @fn void setDeltaIncCount(int deltaIncCount)
     * @brief Set the number of new connections to create once running out the
     *        connection pool.
     * @param deltaIncCount Default to be 10.
     */
    public void setDeltaIncCount(int deltaIncCount) {
        _deltaIncCount = deltaIncCount;
    }

    /**
     * @fn void setMaxIdleCount(int maxIdleCount)
     * @brief Set the max number of the idle connection left in connection
     *        pool after periodically cleaning.
     * @param maxIdleCount Default to be 10.
     * @since v1.12.6 and v2.2
     */
    public void setMaxIdleCount(int maxIdleCount) {
        _maxIdleCount = maxIdleCount;
    }

    /**
     * @fn void setMaxCount(int maxCount)
     * @brief Set the capacity of the connection pool.
     * @param  maxCount Default to be 500.
     * @note When maxCount is set to 0, the connection pool will be disabled.
     * @see Sequoiadb::disableDatasource()
     * @since v1.12.6 and v2.2
     */
    public void setMaxCount(int maxCount) {
        _maxCount = maxCount;
    }

    /**
     * @fn void setKeepAliveTimeout(int keepAliveTimeout)
     * @brief Set the time in milliseconds for abandoning a connection which keep alive time is up.
     *        If a connection has not be used(send and receive) for a long time(longer
     *        than "keepAliveTimeout"), the pool will not let it come back.
     *        The pool will also clean this kind of idle connections in the pool periodically.
     * @param keepAliveTimeout Default to be 0ms, means not care about how long does a connection
     *                         have not be used(send and receive).
     * @note When "keepAliveTimeout" is not set to 0, it's better to set it
     *       greater than "checkInterval" triple over. Besides, unless you know what you need,
     *       never enable this option.
     * @since v1.12.6 and v2.2
     */
    public void setKeepAliveTimeout(int keepAliveTimeout) {
        _keepAliveTimeout = keepAliveTimeout;
    }

    /**
     * @fn void setCheckInterval(int checkInterval)
     * @brief Set the checking interval in milliseconds. Every interval,
     *        the pool cleans all the idle connection which keep alive time is up,
     *        and keeps the number of idle connection not more than "maxIdleCount".
     * @param checkInterval Default to be 60,000ms.
     * @note When "keepAliveTimeout" is not be 0, "checkInterval" should be less than it.
     *       It's better to set "keepAliveTimeout" greater than "checkInterval" triple over.
     * @since v1.12.6 and v2.2
     */
    public void setCheckInterval(int checkInterval) {
        _checkInterval = checkInterval;
    }

    /**
     * @fn void setSyncCoordInterval(int syncCoordInterval)
     * @brief Set the interval for updating coord's addresses from catalog in milliseconds.
     * @param syncCoordInterval Default to be 0ms.
     * @note The updated coord addresses will cover the addresses in the pool.
     *       The offered value can not less than 0. When it is 0, the pool will
     *       stop updating coord's addresses from catalog, and when it is less than
     *       60,000 milliseconds, use 60,000 milliseconds instead.
     * @since v1.12.6 and v2.2
     */
    public void setSyncCoordInterval(int syncCoordInterval) {
        if (syncCoordInterval > 0 && syncCoordInterval < 60000) {
            _syncCoordInterval = 60000;
        } else {
            _syncCoordInterval = syncCoordInterval;
        }
    }

    /**
     * @fn void setValidateConnection(boolean validateConnection)
     * @brief When a idle connection is got out of pool, we need
     *        to validate whether it can be used or not.
     * @param validateConnection Default to be false.
     * @since v1.12.6 and v2.2
     */
    public void setValidateConnection(boolean validateConnection) {
        _validateConnection = validateConnection;
    }

    /**
     * @fn void setConnectStrategy(ConnectStrategy strategy)
     * @brief Set connection strategy.
     * @param strategy Should one of the follow:
     *                 ConnectStrategy.SERIAL,
     *                 ConnectStrategy.RANDOM,
     *                 ConnectStrategy.LOCAL,
     *                 ConnectStrategy.BALANCE
     * @note When choosing ConnectStrategy.LOCAL, if there have no local coord address,
     *       use other address instead.
     * @since v1.12.6 and v2.2
     */
    public void setConnectStrategy(ConnectStrategy strategy) {
        if (strategy == ConnectStrategy.BALANCE) {
            _connectStrategy = ConnectStrategy.SERIAL;
        } else {
            _connectStrategy = strategy;
        }
    }

    /**
     * Set Preferred instance for read request in the session..
     * When user does not set any preferred instance,
     * use the setting in the coord's setting file.
     * Note: When specifying preferred instance, Datasource will set the session attribute only
     * when it creating a connection. That means when user get a connection out from the Datasource,
     * if user reset the session attribute of the connection, Datasource will keep the latest changes of the setting.
     *
     * @param PreferedInstance Could be single value in "M", "m", "S", "s", "A", "a", "1"-"255", or multiple values of them.
     *          <ul>
     *              <li>"M", "m": read and write instance( master instance ). If multiple numeric instances are given with "M", matched master instance will be chosen in higher priority. If multiple numeric instances are given with "M" or "m", master instance will be chosen if no numeric instance is matched.</li>
     *              <li>"S", "s": read only instance( slave instance ). If multiple numeric instances are given with "S", matched slave instances will be chosen in higher priority. If multiple numeric instances are given with "S" or "s", slave instance will be chosen if no numeric instance is matched.</li>
     *              <li>"A", "a": any instance.</li>
     *              <li>"1"-"255": the instance with specified instance ID.</li>
     *              <li>If multiple alphabet instances are given, only first one will be used.</li>
     *              <li>If matched instance is not found, will choose instance by random.</li>
     *          </ul>
     */
    public void setPreferedInstance(final List<String> preferedInstance) {
        if (preferedInstance == null || preferedInstance.size() == 0) {
            return;
        }
        List<String> list = new ArrayList<String>();

        for(String s : preferedInstance) {
            if (isValidMode(s)) {
                if (!list.contains(s)) {
                    list.add(s);
                }
            } else {
                throw new BaseException(SDBError.SDB_INVALIDARG, "invalid preferred instance: " + s);
            }
        }
        if (list.size() == 0) {
            return;
        }
        _preferedInstance = new ArrayList<Object>();
        for(String s : list) {
            try {
                _preferedInstance.add(Integer.valueOf(s));
            } catch(NumberFormatException e) {
                _preferedInstance.add(s);
            }
        }
    }

    /**
     * Set the mode to choose query instance when multiple preferred instances are found in the session.
     *
     * @param mode can be one of the follow, default to be "random".
     *                    <ul>
     *                        <li>"random": choose the instance from matched instances by random.</li>
     *                        <li>"ordered": choose the instance from matched instances by the order of "PreferedInstance".</li>
     *                    </ul>
     */
    public void setPreferedInstanceMode(String mode) {
        if (mode == null || mode.isEmpty()) {
            _preferedInstanceMode = DEFAULT_PREFERRD_INSTANCE_MODE;
        } else {
            _preferedInstanceMode = mode;
        }
    }

    /**
     * Set the timeout (in ms) for operations in the session. -1 means no timeout for operations. Default te be -1.
     *
     * @param timeout The timeout (in ms) for operations in the session.
     */
    public void setSessionTimeout(int timeout) {
        if (timeout < 0) {
            _sessionTimeout = DEFAULT_SESSION_TIMEOUT;
        } else {
            _sessionTimeout = timeout;
        }
    }

    /**
     * @fn int getDeltaIncCount()
     * @brief Get the number of connections to create once running out the
     *        connection pool.
     * @setDeltaIncCount
     */
    public int getDeltaIncCount() {
        return _deltaIncCount;
    }

    /**
     * @fn int getMaxIdleCount()
     * @brief Get the max number of idle connection.
     * @return The max number of idle connection after checking.
     */
    public int getMaxIdleCount() {
        return _maxIdleCount;
    }

    /**
     * @fn int getMaxCount()
     * @brief Get the capacity of the pool.
     * @return The capacity of the pool.
     */
    public int getMaxCount() {
        return _maxCount;
    }

    /**
     * @fn int getKeepAliveTimeout()
     * @brief Get the setup time for abandoning a connection
     *        which has not been used for long time.
     * @return the time
     * @since v1.12.6 and v2.2
     */
    public int getKeepAliveTimeout() {
        return _keepAliveTimeout;
    }

    /**
     * @fn int getCheckInterval()
     * @brief Get the interval for checking the idle connections periodically.
     * @return the interval
     * @since v1.12.6 and v2.2
     */
    public int getCheckInterval() {
        return _checkInterval;
    }

    /**
     * @fn int getSyncCoordInterval()
     * @brief Get the interval for updating coord's addresses from catalog periodically.
     * @return the interval
     * @since v1.12.6 and v2.2
     */
    public int getSyncCoordInterval() {
        return _syncCoordInterval;
    }

    /**
     * @fn boolean getValidateConnection()
     * @brief Get whether to validate a
     *        connection which is got from the pool or not.
     * @return true or false
     * @since v1.12.6 and v2.2
     */
    public boolean getValidateConnection() {
        return _validateConnection;
    }

    /**
     * @fn ConnectStrategy getConnectStrategy()
     * @brief Get the current strategy of creating connections.
     * @return the strategy
     * @since v1.12.6 and v2.2
     */
    public ConnectStrategy getConnectStrategy() {
        return _connectStrategy;
    }

    /**
     * Get the preferred instance.
     * @return The preferred instance or null for no any setting.
     */
    public List<String> getPreferedInstance() {
        if (_preferedInstance == null) {
            return null;
        }
        List<String> list = new ArrayList<String>();
        for(Object o : _preferedInstance) {
            if (o instanceof String) {
                list.add((String)o);
            } else if(o instanceof Integer) {
                list.add(o + "");
            }
        }
        return list;
    }

    List<Object> getPreferedInstanceObjects() {
        return _preferedInstance;
    }

    /**
     * Get the preferred instance node.
     *
     * @return The preferred instance node.
     */
    public String getPreferedInstanceMode() {
        return _preferedInstanceMode;
    }

    /**
     * The Session timeout value.
     * @return Session timeout value.
     */
    public int getSessionTimeout() {
        return _sessionTimeout;
    }



    /**
     * @fn void setInitConnectionNum(int initConnectionNum)
     * @brief Set the initial number of connection.
     * @param  initConnectionNum default to be 10
     * @deprecated Does not work since v1.12.6 and v2.2.
     *             When the connection pool is enabled, the first time to get connection,
     *             the pool increases "deltaIncCount" number of connections. Used
     *             setDeltaIncCount() instead.
     * @see setDeltaIncCount()
     */
    public void setInitConnectionNum(int initConnectionNum) {
    }

    /**
     * @fn void setMaxIdeNum(int maxIdeNum)
     * @brief Set the max number of the idle connection left in connection
     *        pool after periodically cleaning.
     * @param maxIdeNum default to be 10
     * @deprecated Does not work since v1.12.6 and v2.2.
     *             Used setMaxIdleCount() instead.
     * @see setMaxIdleCount()
     */
    public void setMaxIdeNum(int maxIdeNum) {
        setMaxIdleCount(maxIdeNum);
    }

    /**
     * @fn void setMaxConnectionNum(int maxConnectionNum)
     * @brief Set the max number of connection for use. When maxConnectionNum is 0,
     *        the connection pool doesn't really work. In this situation, when request comes,
     *        it builds a connection and return it directly. When a connection goes back to pool,
     *        it disconnects the connection directly and will not put the connection back to pool.
     * @param  maxConnectionNum default to be 500
     * @deprecated Does not work since v1.12.6 and v2.2.
     *             Used setMaxCount() instead.
     * @see setMaxCount()
     */
    public void setMaxConnectionNum(int maxConnectionNum) {
        setMaxCount(maxConnectionNum);
    }

    /**
     * @fn void setTimeout(int timeout)
     * @brief Set the wait time in milliseconds. If the number of connection reaches
     *        maxConnectionNum, the pool can't offer connection immediately, the
     *        requests will be blocked to wait for a moment. When timeout, and there is
     *        still no available connection, connection pool throws exception
     * @param timeout Default to be 5 * 1000ms.
     * @deprecated Does not work since v1.12.6 and v2.2.
     *             Used Sequoiadb.getConnection(int timeout) instead.
     * @see Sequoiadb.getConnection(int timeout)
     * @since v1.12.6 and v2.2
     */
    public void setTimeout(int timeout) {
    }

    /**
     * @fn void setRecheckCyclePeriod(int recheckCyclePeriod)
     * @brief Set the recheck cycle in milliseconds. In each cycle
     *        connection pool cleans all the discardable connection,
     *        and keep the number of valid connection not more than maxIdeNum.
     * @param recheckCyclePeriod recheckCyclePeriod should be less than abandonTime. Default to be 1 * 60 * 1000ms
     * @note It's better to set abandonTime greater than recheckCyclePeriod twice over.
     * @deprecated Does not work since v1.12.6 and v2.2.
     *             Used setCheckInterval() instead.
     * @see setCheckInterval()
     */
    public void setRecheckCyclePeriod(int recheckCyclePeriod) {
        setCheckInterval(recheckCyclePeriod);
    }

    /**
     * @fn void setRecaptureConnPeriod(int recaptureConnPeriod)
     * @brief Set the time in milliseconds for getting back the useful address.
     *        When offer several addresses for connection pool to use, if
     *        some of them are not available(invalid address, network error, coord shutdown,
     *        catalog replica group is not available), we will put these addresses
     *        into a queue, and check them periodically. If some of them is valid again,
     *        get them back for use;
     * @param recaptureConnPeriod default to be 30 * 1000ms
     * @deprecated Does not work since v1.12.6 and v2.2.
     *             The pool will test the invalid address automatically every 30 seconds.
     */
    public void setRecaptureConnPeriod(int recaptureConnPeriod) {
    }

    /**
     * @fn void setAbandonTime(int abandonTime)
     * @brief Set the time in milliseconds for abandoning discardable connection.
     *        If a connection has not be used for a long time(longer than abandonTime),
     *        connection pool would not let it come back to pool. And it will clean this
     *        kind of connections in the pool periodically.
     * @param abandonTime default to be 10 * 60 * 1000ms
     * @note It's better to set abandonTime greater than recheckCyclePeriod twice over.
     * @deprecated Does not work since v1.12.6 and v2.2.
     *             Used setKeepAliveTimeout() instead.
     * @see setKeepAliveTimeout()
     */
    public void setAbandonTime(int abandonTime) {
        setKeepAliveTimeout(abandonTime);
    }

    /**
     * @fn int getInitConnectionNum()
     * @brief Get the setup number of initial connection.
     * @deprecated Does not work since v1.12.6 and v2.2.
     *             Return 0 instead.
     */
    public int getInitConnectionNum() {
        return 0;
    }

    /**
     * @fn int getMaxConnectionNum()
     * @brief Get the max number of connection.
     * @deprecated Does not work since v1.12.6 and v2.2.
     *             Return 0. Used getMaxCount() instead.
     * @see getMaxCount()
     */
    public int getMaxConnectionNum() {
        return getMaxCount();
    }

    /**
     * @fn int getMaxIdeNum()
     * @brief Get the max number of the idle connection.
     * @deprecated Does not work since v1.12.6 and v2.2.
     *             Return 0. Used getMaxIdleCount() instead.
     * @see getMaxIdleCount()
     */
    public int getMaxIdeNum() {
        return getMaxIdleCount();
    }

    /**
     * @fn int getAbandonTime()
     * @brief Get the setup time for abandoning a connection
     *        which is not used for long time.
     * @deprecated Does not work since v1.12.6 and v2.2.
     *             Used getKeepAliveTimeout() instead.
     * @see getKeepAliveTimeout()
     */
    public int getAbandonTime() {
        return getKeepAliveTimeout();
    }

    /**
     * @fn int getRecheckCyclePeriod()
     * @brief get the cycle for checking
     * @deprecated Does not work since v1.12.6 and v2.2.
     *             Used getKeepAliveTimeout() instead.
     * @see getCheckInterval()
     */
    public int getRecheckCyclePeriod() {
        return getCheckInterval();
    }

    /**
     * @fn int getRecaptureConnPeriod()
     * @brief Get the period for getting back useful addresses
     * @deprecated Does not work since v1.12.6 and v2.2. Return 0.
     */
    public int getRecaptureConnPeriod() {
        return 0;
    }

    /**
     * @fn int getTimeout()
     * @brief get the wait time.
     * @deprecated Does not work since v1.12.6 and v2.2. Return 0.
     */
    public int getTimeout() {
        return 0;
    }

    BSONObject getSessionAttr() {
        BSONObject obj = new BasicBSONObject();
        if (_preferedInstance != null && _preferedInstance.size() > 0) {
            BSONObject list = new BasicBSONList();
            int i = 0;
            for(Object o : _preferedInstance) {
                list.put("" + i++, o);
            }
            obj.put(FIELD_NAME_PREFERED_INSTANCE, list);
            obj.put(FIELD_NAME_PREFERED_INSTANCE_MODE, _preferedInstanceMode);
            obj.put(FIELD_NAME_SESSION_TIMEOUT, _sessionTimeout);
        }
        return obj;
    }

    private boolean isCharMode(String s) {
        for(int i = 0; i < MODE.size(); i++) {
            if (MODE.get(i).equals(s)) {
                return true;
            }
        }
        return false;
    }

    private boolean isValidMode(String s) {
        if (isCharMode(s)) {
            return true;
        } else {
            int n = 0;
            try {
                n = Integer.parseInt(s);
            } catch (NumberFormatException e) {
                return false;
            }
            if (n >= 1 && n <= 255) {
                return true;
            } else {
                return false;
            }
        }
    }

}
