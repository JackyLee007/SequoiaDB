package com.sequoiadb.datasource;

import com.sequoiadb.exception.BaseException;
import com.sequoiadb.exception.SDBError;

import java.util.*;


class CountInfo implements Comparable<CountInfo> {
    private String _address;
    private int _usedCount;
    private boolean _hasLeftIdleConn;

    public CountInfo(String addr, int usedCount, boolean hasLeft) {
        _address = addr;
        _usedCount = usedCount;
        _hasLeftIdleConn = hasLeft;
    }

    public void setAddress(String addr) {
        _address = addr;
    }

    public String getAddress() {
        return _address;
    }

    public boolean getHasLeftIdleConn() {
        return _hasLeftIdleConn;
    }

    public void setHasLeftIdleConn(boolean val) {
        _hasLeftIdleConn = val;
    }

    public void changeCount(int change) {
        _usedCount += change;
    }

    @Override
    public int compareTo(CountInfo other) {
        if (this._hasLeftIdleConn == true && other._hasLeftIdleConn == false) {
            return -1;
        } else if (this._hasLeftIdleConn == false && other._hasLeftIdleConn == true) {
            return 1;
        } else {
            if (this._usedCount != other._usedCount) {
                return this._usedCount - other._usedCount;
            } else {
                return this._address.compareTo(other._address);
            }
        }
    }

    @Override
    public String toString() {
        return String.format("{ %s, %d, %b}", _address, _usedCount, _hasLeftIdleConn);
    }
}

class ConcreteBalanceStrategy implements IConnectStrategy {
    private HashMap<String, ArrayDeque<ConnItem>> _idleConnItemMap = new HashMap<String, ArrayDeque<ConnItem>>();
    private HashMap<String, CountInfo> _countInfoMap = new HashMap<String, CountInfo>();
    private TreeSet<CountInfo> _countInfoSet = new TreeSet<CountInfo>();
    private static CountInfo _dumpCountInfo = new CountInfo("", 0, false);

    @Override
    public synchronized void init(List<String> addressList, List<Pair> _idleConnPairs,
                                  List<Pair> _usedConnPairs) {
        Iterator<String> itr1 = addressList.iterator();
        while (itr1.hasNext()) {
            String addr = itr1.next();
            if (!_idleConnItemMap.containsKey(addr)) {
                _idleConnItemMap.put(addr, new ArrayDeque<ConnItem>());
                CountInfo obj = new CountInfo(addr, 0, false);
                _countInfoMap.put(addr, obj);
                _countInfoSet.add(obj);
            }
        }

        Iterator<Pair> itr2 = null;
        if (_idleConnPairs != null) {
            itr2 = _idleConnPairs.iterator();
            while (itr2.hasNext()) {
                Pair pair = itr2.next();
                ConnItem item = pair.first();
                String addr = item.getAddr();
                if (!_idleConnItemMap.containsKey(addr)) {
                    ArrayDeque<ConnItem> deque = new ArrayDeque<ConnItem>();
                    deque.add(item);
                    _idleConnItemMap.put(addr, deque);
                    CountInfo info = new CountInfo(addr, 0, true);
                    _countInfoMap.put(addr, info);
                    _countInfoSet.add(info);
                } else {
                    ArrayDeque<ConnItem> deque = _idleConnItemMap.get(addr);
                    deque.add(item);
                    CountInfo info = _countInfoMap.get(addr);
                    if (info.getHasLeftIdleConn() == false) {
                        _countInfoSet.remove(info);
                        info.setHasLeftIdleConn(true);
                        _countInfoSet.add(info);
                    }
                }
            }
        }

        if (_usedConnPairs != null) {
            itr2 = _usedConnPairs.iterator();
            while (itr2.hasNext()) {
                Pair pair = itr2.next();
                ConnItem item = pair.first();
                String addr = item.getAddr();
                if (_idleConnItemMap.containsKey(addr)) {
                    CountInfo info = _countInfoMap.get(addr);
                    _countInfoSet.remove(info);
                    info.changeCount(1);
                    _countInfoSet.add(info);
                } else {
                    continue;
                }
            }
        }

    }

    @Override
    public synchronized ConnItem pollConnItemForGetting() {
        return _pollConnItem(Operation.GET_CONN);
    }

    @Override
    public synchronized ConnItem pollConnItemForDeleting() {
        return _pollConnItem(Operation.DEL_CONN);
    }

    @Override
    public synchronized ConnItem peekConnItemForDeleting() {
        ConnItem connItem = null;
        while (true) {
            CountInfo countInfo = null;
            String addr = null;
            countInfo = _countInfoSet.lower(_dumpCountInfo);
            if (countInfo == null || countInfo.getHasLeftIdleConn() == false) {
                return null;
            }
            addr = countInfo.getAddress();
            ArrayDeque<ConnItem> deque = _idleConnItemMap.get(addr);
            if (deque != null) {
                connItem = deque.peekLast();// .pollLast();
            } else {
                throw new BaseException(SDBError.SDB_SYS, "Invalid state in strategy");
            }
            if (connItem == null) {
                countInfo = _countInfoMap.get(addr);
                _countInfoSet.remove(countInfo);
                countInfo.setHasLeftIdleConn(false);
                _countInfoSet.add(countInfo);
                continue;
            } else {
                break;
            }
        }
        return connItem;
    }

    private ConnItem _pollConnItem(Operation operation) {
        ConnItem connItem = null;
        while (true) {
            CountInfo countInfo = null;
            String addr = null;
            if (operation == Operation.GET_CONN) {
                try {
                    countInfo = _countInfoSet.first();
                } catch (NoSuchElementException e) {
                    countInfo = null;
                }
            } else {
                countInfo = _countInfoSet.lower(_dumpCountInfo);
            }
            if (countInfo == null || countInfo.getHasLeftIdleConn() == false) {
                return null;
            }
            addr = countInfo.getAddress();
            ArrayDeque<ConnItem> deque = _idleConnItemMap.get(addr);
            if (deque != null) {
                if (operation == Operation.GET_CONN) {
                    connItem = deque.pollFirst();
                } else {
                    connItem = deque.pollLast();
                }
            } else {
                throw new BaseException(SDBError.SDB_SYS, "Invalid state in strategy");
            }
            if (connItem == null) {
                countInfo = _countInfoMap.get(addr);
                _countInfoSet.remove(countInfo);
                countInfo.setHasLeftIdleConn(false);
                _countInfoSet.add(countInfo);
                continue;
            } else {
                break;
            }
        }
        return connItem;
    }

    @Override
    public synchronized void addConnItemAfterCreating(ConnItem connItem) {
        String addr = connItem.getAddr();
        if (!_idleConnItemMap.containsKey(addr)) {
            _restoreIdleConnItemInfo(addr);
        }
        CountInfo countInfo = _countInfoMap.get(addr);
        if (countInfo == null) {
            throw new BaseException(SDBError.SDB_SYS,
                    "the pool has no information about address: " + addr);
        }
        if (countInfo.getHasLeftIdleConn() == false) {
            _countInfoSet.remove(countInfo);
            countInfo.setHasLeftIdleConn(true);
            _countInfoSet.add(countInfo);
        }

        ArrayDeque<ConnItem> deque = _idleConnItemMap.get(addr);
        if (deque == null) {
            throw new BaseException(SDBError.SDB_SYS,
                    "the pool has no information about address: " + addr);
        }
        deque.addLast(connItem);
    }

    @Override
    public synchronized void addConnItemAfterReleasing(ConnItem connItem) {
        String addr = connItem.getAddr();
        if (!_idleConnItemMap.containsKey(addr)) {
            _restoreIdleConnItemInfo(addr);
        }
        CountInfo countInfo = _countInfoMap.get(addr);
        if (countInfo == null) {
            throw new BaseException(SDBError.SDB_SYS,
                    "the pool has no information about address: " + addr);
        }
        if (countInfo.getHasLeftIdleConn() == false) {
            _countInfoSet.remove(countInfo);
            countInfo.setHasLeftIdleConn(true);
            _countInfoSet.add(countInfo);
        }

        ArrayDeque<ConnItem> deque = _idleConnItemMap.get(addr);
        if (deque == null) {
            throw new BaseException(SDBError.SDB_SYS,
                    "the pool has no information about address: " + addr);
        }
        deque.addFirst(connItem);
    }

    @Override
    public synchronized void removeConnItemAfterCleaning(ConnItem connItem) {
        String addr = connItem.getAddr();
        if (!_idleConnItemMap.containsKey(addr)) {
            _restoreIdleConnItemInfo(addr);
        }
        ArrayDeque<ConnItem> deque = _idleConnItemMap.get(addr);
        if (deque == null) {
            throw new BaseException(SDBError.SDB_SYS,
                    "the pool has no information about address: " + addr);
        }
        if (deque.size() == 0) {
            throw new BaseException(SDBError.SDB_SYS,
                    "the pool has no information about address: " + addr);
        }
        if (deque.remove(connItem) == false) {
            throw new BaseException(SDBError.SDB_SYS,
                    "the pool has no information about address: " + addr);
        }
        if (deque.size() == 0) {
            CountInfo countInfo = _countInfoMap.get(addr);
            _countInfoSet.remove(countInfo);
            countInfo.setHasLeftIdleConn(false);
            _countInfoSet.add(countInfo);
        }
    }

    @Override
    public synchronized void updateUsedConnItemCount(ConnItem connItem, int change) {
        String addr = connItem.getAddr();
        if (_countInfoMap.containsKey(addr)) {
            CountInfo countInfo = _countInfoMap.get(addr);
            if (countInfo == null) {
                throw new BaseException(SDBError.SDB_SYS,
                        "the pool has no information about address: " + addr);
            }
            _countInfoSet.remove(countInfo);
            countInfo.changeCount(change);
            _countInfoSet.add(countInfo);
        }
    }

    @Override
    public synchronized String getAddress() {
        CountInfo info = _countInfoSet.higher(_dumpCountInfo);
        if (info == null) {
            try {
                info = _countInfoSet.first();
            } catch (NoSuchElementException e) {
                return null;
            }
        }
        return info.getAddress();
    }

    @Override
    public synchronized void addAddress(String addr) {

        ArrayDeque<ConnItem> deque = _idleConnItemMap.get(addr);
        if (deque == null) {
            _idleConnItemMap.put(addr, new ArrayDeque<ConnItem>());
            CountInfo info = new CountInfo(addr, 0, false);
            _countInfoMap.put(addr, info);
            _countInfoSet.add(info);
        }
    }

    @Override
    public synchronized List<ConnItem> removeAddress(String addr) {
        List<ConnItem> list = new ArrayList<ConnItem>();
        if (_idleConnItemMap.containsKey(addr)) {
            CountInfo obj = _countInfoMap.remove(addr);
            if (obj != null) {
                _countInfoSet.remove(obj);
            }
            ArrayDeque<ConnItem> deque = _idleConnItemMap.remove(addr);
            if (deque != null) {
                for (ConnItem item : deque) {
                    list.add(item);
                }
            }
        }
        return list;
    }

    private void _restoreIdleConnItemInfo(String addr) {
        addAddress(addr);
    }
}
