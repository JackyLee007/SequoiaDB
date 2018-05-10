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
 */
package com.sequoiadb.base;

import com.sequoiadb.base.SequoiadbConstants.Operation;
import com.sequoiadb.exception.BaseException;
import com.sequoiadb.exception.SDBError;
import com.sequoiadb.net.IConnection;
import com.sequoiadb.util.SDBMessageHelper;
import org.bson.BSONObject;
import org.bson.BasicBSONObject;
import org.bson.types.BasicBSONList;

import java.net.InetAddress;
import java.net.UnknownHostException;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Random;

/**
 * @class ReplicaGroup
 * @brief Database operation interfaces of replica group.
 */
public class ReplicaGroup {
    private String name;
    private int id;
    private Sequoiadb sequoiadb;
    private boolean isCataRG;


    /**
     * @return the current replica group's Sequoiadb
     * @fn Sequoiadb getSequoiadb()
     * @brief Get current replica group's Sequoiadb.
     */
    public Sequoiadb getSequoiadb() {
        return sequoiadb;
    }

    /**
     * @return the current replica group's id
     * @fn int getId()
     * @brief Get current replica group's id.
     */
    public int getId() {
        return id;
    }

    /**
     * @return the current replica group's name
     * @fn String getGroupName()
     * @brief Get current replica group's name.
     */
    public String getGroupName() {
        return name;
    }

    ReplicaGroup(Sequoiadb sdb, int id) {
        this.sequoiadb = sdb;
        this.id = id;
        BSONObject group = sdb.getDetailById(id);
        this.name = group.get(SequoiadbConstants.FIELD_NAME_GROUPNAME)
                .toString();
        this.isCataRG = name.equals(Sequoiadb.CATALOG_GROUP_NAME);
    }

    ReplicaGroup(Sequoiadb sdb, String name) {
        this.sequoiadb = sdb;
        this.name = name;
        BSONObject group = sdb.getDetailByName(name);
        this.isCataRG = (name == Sequoiadb.CATALOG_GROUP_NAME);
        this.id = Integer.parseInt(group.get(
                SequoiadbConstants.FIELD_NAME_GROUPID).toString());
    }

    /**
     * @param status Node.NodeStatus
     * @return the amount of the nodes with the specified status
     * @throws com.sequoiadb.exception.BaseException
     * @fn int getNodeNum(Node.NodeStatus status)
     * @brief Get the amount of the nodes with the specified status.
     */
    public int getNodeNum(Node.NodeStatus status) throws BaseException {
        BSONObject group = sequoiadb.getDetailById(id);
        try {
            Object obj = group.get(SequoiadbConstants.FIELD_NAME_GROUP);
            if (obj == null)
                return 0;
            BasicBSONList list = (BasicBSONList) obj;
            return list.size();
        } catch (BaseException e) {
            throw e;
        } catch (Exception e) {
            throw new BaseException(SDBError.SDB_SYS, e);
        }
    }

    /**
     * @return the detail info
     * @throws com.sequoiadb.exception.BaseException
     * @fn BSONObject getDetail()
     * @brief Get detail info of current replicaGoup
     */
    public BSONObject getDetail() throws BaseException {
        return sequoiadb.getDetailById(id);
    }

    /**
     * @return the master node
     * @throws com.sequoiadb.exception.BaseException
     * @fn Node getMaster()
     * @brief Get the master node of current replica group.
     */
    public Node getMaster() throws BaseException {
        BSONObject groupInfoObj = sequoiadb.getDetailById(id);
        if (groupInfoObj == null) {
            throw new BaseException(SDBError.SDB_CLS_GRP_NOT_EXIST,
                    String.format("no information of group id[%d]", id));
        }
        Object nodesInfoArr = groupInfoObj.get(SequoiadbConstants.FIELD_NAME_GROUP);
        if (nodesInfoArr == null || !(nodesInfoArr instanceof BasicBSONList)) {
            throw new BaseException(SDBError.SDB_SYS,
                    String.format("invalid content[%s] of field[%s]",
                            nodesInfoArr == null ? "null" : nodesInfoArr.toString(), SequoiadbConstants.FIELD_NAME_GROUP));
        }
        BasicBSONList nodesInfoList = (BasicBSONList) nodesInfoArr;
        if (nodesInfoList.isEmpty()) {
            throw new BaseException(SDBError.SDB_CLS_EMPTY_GROUP);
        }
        Object primaryNodeObj = groupInfoObj.get(SequoiadbConstants.FIELD_NAME_PRIMARY);
        if (primaryNodeObj == null) {
            throw new BaseException(SDBError.SDB_RTN_NO_PRIMARY_FOUND);
        } else if (!(primaryNodeObj instanceof Number)) {
            throw new BaseException(SDBError.SDB_SYS, "invalid primary node's information: " + primaryNodeObj.toString());
        } else if (primaryNodeObj.equals(Integer.valueOf(-1))) {
            throw new BaseException(SDBError.SDB_RTN_NO_PRIMARY_FOUND);
        }
        BSONObject primaryData = null;
        Object nodeId;
        for (Object nodeInfoObj : nodesInfoList) {
            BSONObject nodeInfo = (BSONObject) nodeInfoObj;
            nodeId = nodeInfo.get(SequoiadbConstants.FIELD_NAME_NODEID);
            if (nodeId == null) {
                throw new BaseException(SDBError.SDB_SYS, "node id can not be null");
            }
            if (nodeId.equals(primaryNodeObj)) {
                primaryData = nodeInfo;
                break;
            }
        }
        if (primaryData == null) {
            throw new BaseException(SDBError.SDB_SYS, "no information about the primary node in node array");
        }
        nodeId = primaryData.get(SequoiadbConstants.FIELD_NAME_NODEID);
        if (nodeId == null || !(nodeId instanceof Number)) {
            throw new BaseException(SDBError.SDB_SYS,
                    String.format("invalid content[%s] of field[%s]",
                            nodeId == null ? "null" : nodeId.toString(), SequoiadbConstants.FIELD_NAME_NODEID));
        }
        Object hostNameObj = primaryData.get(SequoiadbConstants.FIELD_NAME_HOST);
        if (hostNameObj == null || !(hostNameObj instanceof String)) {
            throw new BaseException(SDBError.SDB_SYS,
                    String.format("invalid content[%s] of field[%s]",
                            hostNameObj == null ? "null" : hostNameObj.toString(), SequoiadbConstants.FIELD_NAME_HOST));
        }
        String hostName = hostNameObj.toString();
        int port = getNodePort(primaryData);
        return new Node(hostName, port, Integer.parseInt(nodeId.toString()), this);
    }

    /**
     * @return the slave node
     * @throws com.sequoiadb.exception.BaseException
     * @fn Node getSlave()
     * @brief Get the random slave of current replica group.
     */
    public Node getSlave() throws BaseException {
        List<Integer> list = new ArrayList<Integer>();
        return getSlave(list);
    }

    private Node getSlave(List<Integer> positions) throws BaseException {
        boolean needGeneratePosition = false;
        List<Integer> validPositions = new ArrayList<Integer>();
        if (positions == null || positions.size() == 0) {
            needGeneratePosition = true;
        } else {
            for (int pos : positions) {
                if (pos < 1 || pos > 7) {
                    throw new BaseException(SDBError.SDB_INVALIDARG,
                            String.format("invalid position(%d) in the list", pos));
                }
                if (!validPositions.contains(pos)) {
                    validPositions.add(pos);
                }
            }
            if (validPositions.size() < 1 || validPositions.size() > 7) {
                throw new BaseException(SDBError.SDB_INVALIDARG,
                        String.format("the number of valid position in the list is %d, it should be in [1, 7]",
                                validPositions.size()));
            }
        }
        BSONObject groupInfoObj = sequoiadb.getDetailById(id);
        if (groupInfoObj == null) {
            throw new BaseException(SDBError.SDB_CLS_GRP_NOT_EXIST,
                    String.format("no information of group id[%d]", id));
        }
        Object nodesInfoArr = groupInfoObj.get(SequoiadbConstants.FIELD_NAME_GROUP);
        if (nodesInfoArr == null || !(nodesInfoArr instanceof BasicBSONList)) {
            throw new BaseException(SDBError.SDB_SYS,
                    String.format("invalid content[%s] of field[%s]",
                            nodesInfoArr == null ? "null" : nodesInfoArr.toString(), SequoiadbConstants.FIELD_NAME_GROUP));
        }
        BasicBSONList nodesInfoList = (BasicBSONList) nodesInfoArr;
        if (nodesInfoList.isEmpty()) {
            throw new BaseException(SDBError.SDB_CLS_EMPTY_GROUP);
        }
        Object primaryNodeId = groupInfoObj.get(SequoiadbConstants.FIELD_NAME_PRIMARY);
        boolean hasPrimary = true;
        if (primaryNodeId == null) {
            hasPrimary = false;
        } else if (!(primaryNodeId instanceof Number)) {
            throw new BaseException(SDBError.SDB_SYS, "invalid primary node's information: " + primaryNodeId.toString());
        } else if (primaryNodeId.equals(Integer.valueOf(-1))) {
            hasPrimary = false;
        }
        int primaryNodePosition = 0;
        for (int i = 0; i < nodesInfoList.size(); i++) {
            BSONObject nodeInfo = (BSONObject) nodesInfoList.get(i);
            Object nodeIdValue = nodeInfo.get(SequoiadbConstants.FIELD_NAME_NODEID);
            if (nodeIdValue == null) {
                throw new BaseException(SDBError.SDB_SYS, "node id can not be null");
            }
            if (hasPrimary && nodeIdValue.equals(primaryNodeId)) {
                primaryNodePosition = i + 1;
            }
        }
        if (hasPrimary && primaryNodePosition == 0) {
            throw new BaseException(SDBError.SDB_SYS, "have no primary node in nodes list");
        }
        int nodeCount = nodesInfoList.size();
        if (needGeneratePosition) {
            for (int i = 0; i < nodeCount; i++) {
                if (hasPrimary && primaryNodePosition == i + 1) {
                    continue;
                }
                validPositions.add(i + 1);
            }
        }
        int nodeIndex = -1;
        BSONObject nodeInfoObj = null;
        if (nodeCount == 1) {
            nodeInfoObj = (BSONObject) nodesInfoList.get(0);
        } else if (validPositions.size() == 1) {
            nodeIndex = (validPositions.get(0) - 1) % nodeCount;
            nodeInfoObj = (BSONObject) nodesInfoList.get(nodeIndex);
        } else {
            int position = 0;
            Random rand = new Random();
            int[] flags = new int[7];
            List<Integer> includePrimaryPositions = new ArrayList<Integer>();
            List<Integer> excludePrimaryPositions = new ArrayList<Integer>();
            for (int pos : validPositions) {
                if (pos <= nodeCount) {
                    nodeIndex = pos - 1;
                    if (flags[nodeIndex] == 0) {
                        flags[nodeIndex] = 1;
                        includePrimaryPositions.add(pos);
                        if (hasPrimary && primaryNodePosition != pos) {
                            excludePrimaryPositions.add(pos);
                        }
                    }
                } else {
                    nodeIndex = (pos - 1) % nodeCount;
                    if (flags[nodeIndex] == 0) {
                        flags[nodeIndex] = 1;
                        includePrimaryPositions.add(pos);
                        if (hasPrimary && primaryNodePosition != nodeIndex + 1) {
                            excludePrimaryPositions.add(pos);
                        }
                    }
                }
            }
            if (excludePrimaryPositions.size() > 0) {
                position = rand.nextInt(excludePrimaryPositions.size());
                position = excludePrimaryPositions.get(position);
            } else {
                position = rand.nextInt(includePrimaryPositions.size());
                position = includePrimaryPositions.get(position);
                if (needGeneratePosition) {
                    position += 1;
                }
            }
            nodeIndex = (position - 1) % nodeCount;
            nodeInfoObj = (BSONObject) nodesInfoList.get(nodeIndex);
        }
        int nodeId = Integer.parseInt(nodeInfoObj.get(SequoiadbConstants.FIELD_NAME_NODEID).toString());
        String hostName = nodeInfoObj.get(SequoiadbConstants.FIELD_NAME_HOST).toString();
        int port = getNodePort(nodeInfoObj);
        return new Node(hostName, port, nodeId, this);
    }

    /**
     * @param nodeName The name of the node
     * @return the specified node
     * @throws com.sequoiadb.exception.BaseException
     * @fn Node getNode(String nodeName)
     * @brief Get node by node's name (IP:PORT).
     */
    public Node getNode(String nodeName) throws BaseException {
        if (nodeName == null || nodeName.isEmpty()) {
            throw new BaseException(SDBError.SDB_INVALIDARG, nodeName);
        }
        String[] nodeMetaInfoArray = nodeName.split(":");
        if (nodeMetaInfoArray.length != 2) {
            throw new BaseException(SDBError.SDB_INVALIDARG, nodeName);
        }
        String inputHostName = nodeMetaInfoArray[0];
        int inputPort = Integer.parseInt((nodeMetaInfoArray[1]));
        Node node = getNodeByMetaInfo(inputHostName, inputPort);
        if (node != null) {
            return node;
        } else {
            throw new BaseException(SDBError.SDB_CLS_NODE_NOT_EXIST, nodeName);
        }
    }

    /**
     * @param hostName host name
     * @param port     port
     * @return the Node object
     * @throws com.sequoiadb.exception.BaseException
     * @fn Node getNode(String hostName, int port)
     * @brief Get node by hostName and port.
     */
    public Node getNode(String hostName, int port) throws BaseException {
        Node node = getNodeByMetaInfo(hostName, port);
        if (node != null) {
            return node;
        } else {
            throw new BaseException(SDBError.SDB_CLS_NODE_NOT_EXIST, hostName + ":" + port);
        }
    }

    /**
     * @param hostName  host name
     * @param port      port
     * @param configure configuration for this operation
     * @return the attach Node object
     * @throws com.sequoiadb.exception.BaseException
     * @fn Node attachNode(String hostName, int port,
     * BSONObject configure)
     * @brief Attach node.
     */
    public Node attachNode(String hostName, int port,
                           BSONObject configure) throws BaseException {
        BSONObject config = new BasicBSONObject();
        config.put(SequoiadbConstants.FIELD_NAME_GROUPNAME, name);
        config.put(SequoiadbConstants.FIELD_NAME_HOST, hostName);
        config.put(SequoiadbConstants.PMD_OPTION_SVCNAME,
                Integer.toString(port));
        config.put(SequoiadbConstants.FIELD_NAME_ONLY_ATTACH, true);
        if (configure != null) {
            for (String key : configure.keySet()) {
                if (key.equals(SequoiadbConstants.FIELD_NAME_GROUPNAME)
                        || key.equals(SequoiadbConstants.FIELD_NAME_HOST)
                        || key.equals(SequoiadbConstants.PMD_OPTION_SVCNAME)
                        || key.equals(SequoiadbConstants.FIELD_NAME_ONLY_ATTACH))
                    continue;

                config.put(key, configure.get(key));
            }
        }
        SDBMessage rtn = adminCommand(SequoiadbConstants.CREATE_CMD,
                SequoiadbConstants.NODE, config);
        int flags = rtn.getFlags();
        if (flags != 0) {
            String msg = "node = " + hostName + ":" + port +
                    ", configure = " + configure;
            throw new BaseException(flags, msg);
        }

        return getNode(hostName, port);
    }

    /**
     * @param hostName  host name
     * @param port      port
     * @param configure configuration for this operation
     * @return void
     * @throws com.sequoiadb.exception.BaseException
     * @fn void detachNode(String hostName, int port,
     * BSONObject configure)
     * @brief Detach node.
     */
    public void detachNode(String hostName, int port,
                           BSONObject configure) throws BaseException {
        BSONObject config = new BasicBSONObject();
        config.put(SequoiadbConstants.FIELD_NAME_GROUPNAME, name);
        config.put(SequoiadbConstants.FIELD_NAME_HOST, hostName);
        config.put(SequoiadbConstants.PMD_OPTION_SVCNAME,
                Integer.toString(port));
        config.put(SequoiadbConstants.FIELD_NAME_ONLY_DETACH, true);
        if (configure != null) {
            for (String key : configure.keySet()) {
                if (key.equals(SequoiadbConstants.FIELD_NAME_GROUPNAME)
                        || key.equals(SequoiadbConstants.FIELD_NAME_HOST)
                        || key.equals(SequoiadbConstants.PMD_OPTION_SVCNAME)
                        || key.equals(SequoiadbConstants.FIELD_NAME_ONLY_DETACH))
                    continue;

                config.put(key, configure.get(key));
            }
        }
        SDBMessage rtn = adminCommand(SequoiadbConstants.REMOVE_CMD,
                SequoiadbConstants.NODE, config);
        int flags = rtn.getFlags();
        if (flags != 0) {
            String msg = "node = " + hostName + ":" + port +
                    ", configure = " + configure;
            throw new BaseException(flags, msg);
        }
    }

    /**
     * @param hostName  host name
     * @param port      port
     * @param dbPath    the path for node
     * @param configure configuration for this operation
     * @return the created Node object
     * @throws com.sequoiadb.exception.BaseException
     * @fn Node createNode(String hostName, int port, String dbPath,
     * Map<String, String> configure)
     * @brief Create node.
     */
    public Node createNode(String hostName, int port, String dbPath,
                           Map<String, String> configure) throws BaseException {
        BSONObject config = new BasicBSONObject();
        config.put(SequoiadbConstants.FIELD_NAME_GROUPNAME, name);
        config.put(SequoiadbConstants.FIELD_NAME_HOST, hostName);
        config.put(SequoiadbConstants.PMD_OPTION_SVCNAME,
                Integer.toString(port));
        config.put(SequoiadbConstants.PMD_OPTION_DBPATH, dbPath);
        if (configure != null && !configure.isEmpty())
            for (String key : configure.keySet()) {
                if (key.equals(SequoiadbConstants.FIELD_NAME_GROUPNAME)
                        || key.equals(SequoiadbConstants.FIELD_NAME_HOST)
                        || key.equals(SequoiadbConstants.PMD_OPTION_SVCNAME))
                    continue;
                config.put(key, configure.get(key));
            }
        SDBMessage rtn = adminCommand(SequoiadbConstants.CREATE_CMD,
                SequoiadbConstants.NODE, config);
        int flags = rtn.getFlags();
        if (flags != 0) {
            String msg = "node = " + hostName + ":" + port +
                    ", dbPath = " + dbPath +
                    ", configure = " + configure;
            throw new BaseException(flags, msg);
        }
        return getNode(hostName, port);
    }

    /**
     * @param hostName  host name
     * @param port      port
     * @param dbPath    the path for node
     * @param configure configuration for this operation
     * @return the created Node object
     * @throws com.sequoiadb.exception.BaseException
     * @fn Node createNode(String hostName, int port, String dbPath,
     * BSONObject configure)
     * @brief Create node.
     */
    public Node createNode(String hostName, int port, String dbPath,
                           BSONObject configure) throws BaseException {
        BSONObject config = new BasicBSONObject();
        config.put(SequoiadbConstants.FIELD_NAME_GROUPNAME, name);
        config.put(SequoiadbConstants.FIELD_NAME_HOST, hostName);
        config.put(SequoiadbConstants.PMD_OPTION_SVCNAME,
                Integer.toString(port));
        config.put(SequoiadbConstants.PMD_OPTION_DBPATH, dbPath);
        if (configure != null && !configure.isEmpty())
            for (String key : configure.keySet()) {
                if (key.equals(SequoiadbConstants.FIELD_NAME_GROUPNAME)
                        || key.equals(SequoiadbConstants.FIELD_NAME_HOST)
                        || key.equals(SequoiadbConstants.PMD_OPTION_SVCNAME))
                    continue;
                config.put(key, configure.get(key));
            }
        SDBMessage rtn = adminCommand(SequoiadbConstants.CREATE_CMD,
                SequoiadbConstants.NODE, config);
        int flags = rtn.getFlags();
        if (flags != 0) {
            String msg = "node = " + hostName + ":" + port +
                    ", dbPath = " + dbPath +
                    ", configure = " + configure;
            throw new BaseException(flags, msg);
        }
        return getNode(hostName, port);
    }

    /**
     * @param hostName  host name
     * @param port      port
     * @param configure configuration for this operation
     * @throws com.sequoiadb.exception.BaseException
     * @fn void removeNode(String hostName, int port,
     * BSONObject configure)
     * @brief Remove node.
     */
    public void removeNode(String hostName, int port,
                           BSONObject configure) throws BaseException {
        BSONObject config = new BasicBSONObject();
        config.put(SequoiadbConstants.FIELD_NAME_GROUPNAME, name);
        config.put(SequoiadbConstants.FIELD_NAME_HOST, hostName);
        config.put(SequoiadbConstants.PMD_OPTION_SVCNAME,
                Integer.toString(port));
        if (configure != null)
            for (String key : configure.keySet()) {
                if (key.equals(SequoiadbConstants.FIELD_NAME_GROUPNAME)
                        || key.equals(SequoiadbConstants.FIELD_NAME_HOST)
                        || key.equals(SequoiadbConstants.PMD_OPTION_SVCNAME))
                    continue;
                config.put(key, configure.get(key));
            }
        SDBMessage rtn = adminCommand(SequoiadbConstants.REMOVE_CMD,
                SequoiadbConstants.NODE, config);
        int flags = rtn.getFlags();
        if (flags != 0) {
            String msg = "node = " + hostName + ":" + port +
                    ", configure = " + configure;
            throw new BaseException(flags, msg);
        }
    }

    /**
     * @return void
     * @throws com.sequoiadb.exception.BaseException
     * @fn void start()
     * @brief Start current replica group.
     */
    public void start() throws BaseException {
        BSONObject groupName = new BasicBSONObject();
        groupName.put(SequoiadbConstants.FIELD_NAME_GROUPNAME, this.name);
        SDBMessage rtn = adminCommand(SequoiadbConstants.ACTIVE_CMD,
                SequoiadbConstants.GROUP, groupName);
        int flags = rtn.getFlags();
        if (flags != 0) {
            throw new BaseException(flags, this.name);
        }
    }

    /**
     * @return void
     * @throws com.sequoiadb.exception.BaseException
     * @fn void stop()
     * @brief Stop current replica group.
     */
    public void stop() throws BaseException {
        BSONObject groupName = new BasicBSONObject();
        groupName.put(SequoiadbConstants.FIELD_NAME_GROUPNAME, this.name);
        SDBMessage rtn = adminCommand(SequoiadbConstants.SHUTDOWN_CMD,
                SequoiadbConstants.GROUP, groupName);
        int flags = rtn.getFlags();
        if (flags != 0) {
            throw new BaseException(flags, this.name);
        }
    }

    /**
     * @return true is while false is not
     * @fn boolean isCatalog()
     * @brief Judge whether current replicaGroup is catalog replica group or not.
     */
    public boolean isCatalog() {
        return isCataRG;
    }

    private Node getNodeByMetaInfo(final String inputHostName, final int inputPort) {
        if (inputHostName == null || inputHostName.isEmpty() || inputPort < 0 || inputPort > 65535) {
            throw new BaseException(SDBError.SDB_INVALIDARG,
                    String.format("invalid node info[%s:%d]", inputHostName, inputPort));
        }
        BSONObject groupInfoObj = sequoiadb.getDetailById(id);
        if (groupInfoObj == null) {
            throw new BaseException(SDBError.SDB_CLS_GRP_NOT_EXIST,
                    String.format("no information of group id[%d]", id));
        }
        Object nodesInfoArr = groupInfoObj.get(SequoiadbConstants.FIELD_NAME_GROUP);
        if (nodesInfoArr == null || !(nodesInfoArr instanceof BasicBSONList)) {
            throw new BaseException(SDBError.SDB_SYS,
                    String.format("invalid content[%s] of field[%s]",
                            nodesInfoArr == null ? "null" : nodesInfoArr.toString(), SequoiadbConstants.FIELD_NAME_GROUP));
        }
        BasicBSONList nodesInfoList = (BasicBSONList) nodesInfoArr;
        if (nodesInfoList.size() == 0) {
            return null;
        }
        Object nodeIdFromCatalog = null;
        String hostNameFromCatalog = null;
        int portFromCatalog = -1;
        for (Object nodeInfoObj : nodesInfoList) {
            BSONObject nodeInfo = (BSONObject) nodeInfoObj;
            nodeIdFromCatalog = nodeInfo.get(SequoiadbConstants.FIELD_NAME_NODEID);
            hostNameFromCatalog = (String) nodeInfo.get(SequoiadbConstants.FIELD_NAME_HOST);
            portFromCatalog = getNodePort(nodeInfo);
            if (nodeIdFromCatalog == null || hostNameFromCatalog == null) {
                throw new BaseException(SDBError.SDB_SYS, "invalid node's information");
            }
            if (hostNameFromCatalog.equals(inputHostName) && portFromCatalog == inputPort) {
                return new Node(inputHostName, inputPort,
                        Integer.parseInt(nodeIdFromCatalog.toString()), this);
            }
        }
        return null;
    }

    private int getNodePort(BSONObject node) {
        if (node == null) {
            throw new BaseException(SDBError.SDB_SYS, "invalid information of node");
        }
        Object services = node.get(SequoiadbConstants.FIELD_NAME_GROUPSERVICE);
        if (services == null)
            throw new BaseException(SDBError.SDB_SYS, node.toString());
        BasicBSONList serviceInfos = (BasicBSONList) services;
        if (serviceInfos.size() == 0)
            throw new BaseException(SDBError.SDB_CLS_NODE_NOT_EXIST);
        int port = -1;
        for (Object obj : serviceInfos) {
            BSONObject service = (BSONObject) obj;
            if (service.get(SequoiadbConstants.FIELD_NAME_SERVICETYPE)
                    .toString().equals("0")) {
                port = Integer.parseInt(service.get(
                        SequoiadbConstants.FIELD_NAME_SERVICENAME).toString());
                break;
            }
        }
        if (port == -1) {
            throw new BaseException(SDBError.SDB_SYS, node.toString());
        }
        return port;
    }

    private SDBMessage adminCommand(String cmdType, String contextType,
                                    BSONObject query) throws BaseException {
        IConnection connection = sequoiadb.getConnection();
        BSONObject dummyObj = new BasicBSONObject();
        SDBMessage sdbMessage = new SDBMessage();
        String commandString = SequoiadbConstants.ADMIN_PROMPT + cmdType + " "
                + contextType;
        if (query != null)
            sdbMessage.setMatcher(query);
        else
            sdbMessage.setMatcher(dummyObj);
        sdbMessage.setCollectionFullName(commandString);
        sdbMessage.setFlags(0);
        sdbMessage.setNodeID(SequoiadbConstants.ZERO_NODEID);
        sdbMessage.setRequestID(sequoiadb.getNextRequstID());
        sdbMessage.setSkipRowsCount(-1);
        sdbMessage.setReturnRowsCount(-1);
        sdbMessage.setSelector(dummyObj);
        sdbMessage.setOrderBy(dummyObj);
        sdbMessage.setHint(dummyObj);
        sdbMessage.setOperationCode(Operation.OP_QUERY);

        byte[] request = SDBMessageHelper.buildQueryRequest(sdbMessage, sequoiadb.endianConvert);
        connection.sendMessage(request);

        ByteBuffer byteBuffer = connection.receiveMessage(sequoiadb.endianConvert);
        SDBMessage rtnSDBMessage = SDBMessageHelper.msgExtractReply(byteBuffer);
        SDBMessageHelper.checkMessage(sdbMessage, rtnSDBMessage);
        return rtnSDBMessage;
    }
}
