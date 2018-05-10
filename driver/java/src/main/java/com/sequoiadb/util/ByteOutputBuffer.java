/**
 * Copyright (C) 2012-2017 SequoiaDB Inc.
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
package com.sequoiadb.util;

import org.bson.io.OutputBuffer;

import java.io.IOException;
import java.io.OutputStream;
import java.nio.ByteOrder;

public class ByteOutputBuffer extends OutputBuffer {
    private IOBuffer ioBuffer;

    ByteOutputBuffer(IOBuffer buffer) {
        ioBuffer = buffer;
    }

    @Override
    public void write(byte[] b) {
        ioBuffer.put(b);
    }

    @Override
    public void write(byte[] b, int off, int len) {
        ioBuffer.put(b, off, len);
    }

    @Override
    public void write(int b) {
        ioBuffer.put((byte) (0xFF & b));
    }

    @Override
    public void writeInt(int x) {
        ioBuffer.putInt(x);
    }

    private static int swapInt32(int i) {
        return (i & 0xFF) << 24
                | (i >> 8 & 0xFF) << 16
                | (i >> 16 & 0xFF) << 8
                | (i >> 24 & 0xFF);
    }

    @Override
    public void writeIntBE(int x) {
        ByteOrder order = ioBuffer.order();
        int xBE = ByteOrder.LITTLE_ENDIAN == order ? swapInt32(x) : x;
        ioBuffer.putInt(xBE);
    }

    @Override
    public void writeInt(int pos, int x) {
        final int save = getPosition();
        setPosition(pos);
        writeInt(x);
        setPosition(save);
    }

    @Override
    public void writeShort(short x) {
        ioBuffer.putShort(x);
    }

    @Override
    public void writeLong(long x) {
        ioBuffer.putLong(x);
    }

    @Override
    public void writeDouble(double x) {
        ioBuffer.putDouble(x);
    }

    @Override
    public int getPosition() {
        return ioBuffer.position();
    }

    @Override
    public void setPosition(int position) {
        ioBuffer.position(position);
    }

    @Override
    public void seekEnd() {
        ioBuffer.position(ioBuffer.capacity());
    }

    @Override
    public void seekStart() {
        ioBuffer.position(0);
    }

    @Override
    public int size() {
        return ioBuffer.capacity();
    }

    @Override
    public int pipe(OutputStream out) throws IOException {
        out.write(ioBuffer.array());
        return ioBuffer.capacity();
    }
}
