// SPDX-License-Identifier: MIT
// JNI interface for the MIT-licensed Andro-NSZ converter (pinned source).
package com.androNSZ
object NszConverter {
    init { System.loadLibrary("AndroNSZ") }
    interface ProgressCallback { fun onProgress(done: Long, total: Long) }
    interface StatusCallback { fun onStatus(tag: String, msg: String) }
    @JvmStatic external fun nativeConvert(inputPath: String, outputPath: String,
        progressCallback: ProgressCallback?, statusCallback: StatusCallback?): Int
    @JvmStatic external fun nativeConvertXcz(inputPath: String, outputPath: String,
        progressCallback: ProgressCallback?, statusCallback: StatusCallback?): Int
    @JvmStatic external fun nativeCancel()
    @JvmStatic external fun nativeErrorString(errorCode: Int): String
    @JvmStatic external fun nativeSetVerification(enabled: Boolean,
        headerKey: ByteArray?, keyAreaKeys: ByteArray?)
}
