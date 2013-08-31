/*
 *  Licensed to the Apache Software Foundation (ASF) under one or more
 *  contributor license agreements.  See the NOTICE file distributed with
 *  this work for additional information regarding copyright ownership.
 *  The ASF licenses this file to You under the Apache License, Version 2.0
 *  (the "License"); you may not use this file except in compliance with
 *  the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

package java.nio.charset;

/**
 * An {@code UnsupportedCharsetException} is thrown when an unsupported charset
 * name is encountered.
 */
public class UnsupportedCharsetException extends IllegalArgumentException {

    /*
     * This constant is used during deserialization to check the version
     * which created the serialized object.
     */
    private static final long serialVersionUID = 1490765524727386367L;

    // the unsupported charset name
    private String charsetName;

    /**
     * Constructs a new {@code UnsupportedCharsetException} with the supplied
     * charset name.
     *
     * @param charsetName
     *            the encountered unsupported charset name.
     */
    public UnsupportedCharsetException(String charsetName) {
        super((charsetName != null) ? charsetName : "null");
        this.charsetName = charsetName;
    }

    /**
     * Gets the encountered unsupported charset name.
     *
     * @return the encountered unsupported charset name.
     */
    public String getCharsetName() {
        return charsetName;
    }
}
