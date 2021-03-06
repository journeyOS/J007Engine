/*
 * Copyright (c) 2021 anqi.huang@outlook.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.journeyOS.J007engine.help;

import android.content.ActivityNotFoundException;
import android.content.Intent;

import com.journeyOS.J007engine.BuildConfig;
import com.journeyOS.J007engine.core.J007Core;
import com.journeyOS.J007engine.utils.SmartLog;

import java.io.FileDescriptor;
import java.io.PrintWriter;

/**
 * @author solo
 * handle adb command
 * adb shell dumpsys activity service com.journeyOS.J007engine/com.journeyOS.J007engine.service.J007EngineService -h(-help)
 */
public class DumpManager {
    private static final String TAG = DumpManager.class.getSimpleName();
    private static final String H = "-h";
    private static final String HELP = "-help";

    private static final String D = "-d";
    private static final String DEBUG = "-debug";

    public static void dump(FileDescriptor fd, PrintWriter writer, String[] args) {
        if (writer == null || args == null) {
            SmartLog.w(TAG, "writer or args wasn't null");
            return;
        }

        int opti = 0;
        while (opti < args.length) {
            String opt = args[opti];
            if ((H.equals(opt) || HELP.equals(opt))) {
                dumpHelp(writer);
                return;
            }

            if ((D.equals(opt) || DEBUG.equals(opt))) {
                dumpDebug();
                return;
            }

            //TODO
        }
    }

    private static void dumpHelp(PrintWriter writer) {
        writer.println("---------------------------------------------------------------");
        writer.println("build version name = " + BuildConfig.BUILD_VERSION_NAME);
        writer.println("---------------------------------------------------------------");
        writer.println("  help");
        writer.println("      Print this help text.");
        writer.println("      adb shell dumpsys activity service com.journeyOS.J007engine/com.journeyOS.J007engine.service.J007EngineService arg0 arg1 ...");
        writer.println("---------------------------------------------------------------");
        //TODO
    }

    private static void dumpDebug() {
        try {
            Intent intent = new Intent("com.journeyOS.J007engine.action.DEBUG");
            intent.setPackage(J007Core.getCore().getContext().getPackageName());
            intent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            J007Core.getCore().getContext().startActivity(intent);
        } catch (ActivityNotFoundException e) {
            e.printStackTrace();
        }
    }
}
