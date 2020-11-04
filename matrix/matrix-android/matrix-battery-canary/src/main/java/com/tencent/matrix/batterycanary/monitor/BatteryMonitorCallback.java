package com.tencent.matrix.batterycanary.monitor;

import android.app.AlarmManager;
import android.app.PendingIntent;
import android.os.HandlerThread;
import android.os.Process;
import android.support.annotation.CallSuper;
import android.support.annotation.NonNull;
import android.support.annotation.Nullable;
import android.support.v4.util.Consumer;
import android.util.LongSparseArray;

import com.tencent.matrix.Matrix;
import com.tencent.matrix.batterycanary.monitor.feature.AlarmMonitorFeature;
import com.tencent.matrix.batterycanary.monitor.feature.DeviceStatMonitorFeature;
import com.tencent.matrix.batterycanary.monitor.feature.DeviceStatMonitorFeature.BatteryTmpSnapshot;
import com.tencent.matrix.batterycanary.monitor.feature.DeviceStatMonitorFeature.CpuFreqSnapshot;
import com.tencent.matrix.batterycanary.monitor.feature.JiffiesMonitorFeature;
import com.tencent.matrix.batterycanary.monitor.feature.LooperTaskMonitorFeature;
import com.tencent.matrix.batterycanary.monitor.feature.MonitorFeature.Snapshot.Delta;
import com.tencent.matrix.batterycanary.monitor.feature.WakeLockMonitorFeature;
import com.tencent.matrix.batterycanary.monitor.feature.WakeLockMonitorFeature.WakeLockTrace.WakeLockRecord;
import com.tencent.matrix.util.MatrixLog;

import java.util.Arrays;
import java.util.List;

/**
 * @author Kaede
 * @since 2020/10/27
 */
public interface BatteryMonitorCallback extends JiffiesMonitorFeature.JiffiesListener, LooperTaskMonitorFeature.LooperTaskListener, WakeLockMonitorFeature.WakeLockListener, AlarmMonitorFeature.AlarmListener {

    @SuppressWarnings("NotNullFieldNotInitialized")
    class BatteryPrinter implements BatteryMonitorCallback {
        private static final String TAG = "Matrix.BatteryPrinter";
        private static final int ONE_MIN = 60 * 1000;

        @NonNull
        private BatteryMonitorCore mMonitorCore;
        private final Printer mPrinter = new Printer();
        private final LongSparseArray<List<LooperTaskMonitorFeature.TaskTraceInfo>> tasks = new LongSparseArray<>();
        private WakeLockMonitorFeature.WakeLockInfo mLastWakeWakeLockInfo = null;
        private int diffWakeCount = 0;
        private long diffWakeTime = 0L;

        @Nullable private JiffiesMonitorFeature.JiffiesResult lastJiffiesResult = null;
        @Nullable private CpuFreqSnapshot lastCpuFreqSnapshot = null;
        @Nullable private BatteryTmpSnapshot lastBatteryTmpSnapshot = null;

        @SuppressWarnings("UnusedReturnValue")
        final BatteryPrinter attach(BatteryMonitorCore monitorCore) {
            mMonitorCore = monitorCore;
            return this;
        }

        @Override
        public void onTraceBegin() {
            WakeLockMonitorFeature plugin = mMonitorCore.getMonitorFeature(WakeLockMonitorFeature.class);
            if (null != plugin) {
                mLastWakeWakeLockInfo = plugin.currentWakeLocksInfo();
            }
        }

        @Override
        public void onTraceEnd() {
            WakeLockMonitorFeature plugin = mMonitorCore.getMonitorFeature(WakeLockMonitorFeature.class);
            if (null != plugin && null != mLastWakeWakeLockInfo) {
                WakeLockMonitorFeature.WakeLockInfo wakeLockInfo = plugin.currentWakeLocksInfo();
                diffWakeCount = wakeLockInfo.totalWakeLockCount - mLastWakeWakeLockInfo.totalWakeLockCount;
                diffWakeTime = wakeLockInfo.totalWakeLockTime - mLastWakeWakeLockInfo.totalWakeLockTime;
            }
        }

        @Override
        public void onJiffies(JiffiesMonitorFeature.JiffiesResult result) {
            lastJiffiesResult = result;
            onCanaryDump();
        }

        @Override
        public void onTaskTrace(Thread thread, List<LooperTaskMonitorFeature.TaskTraceInfo> sortList) {
            if (thread instanceof HandlerThread) {
                synchronized (tasks) {
                    tasks.put(((HandlerThread) thread).getThreadId(), sortList);
                }
            }
        }

        @Override
        public void onWakeLockTimeout(int warningCount, WakeLockRecord record) {
        }

        @Override
        public void onAlarmDuplicated(int duplicatedCount, AlarmMonitorFeature.AlarmRecord record) {
        }

        @CallSuper
        protected void onCanaryDump() {
            mPrinter.clear();

            // title
            mPrinter.writeTitle();
            JiffiesMonitorFeature.JiffiesResult result = lastJiffiesResult;
            if (result != null) {
                // header
                mPrinter.append("| ").append("pid=").append(Process.myPid())
                        .tab().append("stat=").append(result.status)
                        .tab().append("during(min)=").append(result.upTimeDiff / ONE_MIN).append("<").append(result.timeDiff / ONE_MIN)
                        .tab().append("diff(jiffies)=").append(result.totalJiffiesDiff)
                        .tab().append("avg(jiffies/min)=").append(result.totalJiffiesDiff / Math.max(1, result.upTimeDiff / ONE_MIN))
                        .enter();

                // jiffies sections
                mPrinter.createSection("jiffies");
                for (JiffiesMonitorFeature.JiffiesResult.ThreadJiffies threadJiffies : result.threadJiffies.subList(0, Math.min(result.threadJiffies.size(), 8))) {
                    if (threadJiffies.jiffiesDiff <= 0) {
                        mPrinter.append("|\t\t......\n");
                        break;
                    }
                    mPrinter.append("| -> ").append(threadJiffies).append("\n");
                    List<LooperTaskMonitorFeature.TaskTraceInfo> threadTasks = tasks.get(threadJiffies.threadInfo.tid);
                    if (null != threadTasks && !threadTasks.isEmpty()) {
                        for (LooperTaskMonitorFeature.TaskTraceInfo task : threadTasks.subList(0, Math.min(3, threadTasks.size()))) {
                            mPrinter.append("|\t\t").append(task).append("\n");
                        }
                    }
                }

                mPrinter.append("| -> ").append("incrementWakeCount=").append(diffWakeCount).append(" sumWakeTime=").append(diffWakeTime).append("ms\n");
                mPrinter.append(getExtInfo());
            }

            onWritingSections();

            // end
            mPrinter.writeEnding();
            mPrinter.dump();
            synchronized (tasks) {
                tasks.clear();
            }
        }

        @CallSuper
        protected void onWritingSections() {
            final DeviceStatMonitorFeature deviceStatMonitor = mMonitorCore.getMonitorFeature(DeviceStatMonitorFeature.class);
            if (deviceStatMonitor != null) {
                // Cpu Freq
                createSection("device_stat", new Consumer<Printer>() {
                    @Override
                    public void accept(Printer printer) {
                        CpuFreqSnapshot cpuFreqSnapshot = deviceStatMonitor.currentCpuFreq();
                        if (lastCpuFreqSnapshot != null) {
                            final Delta<CpuFreqSnapshot> cpuFreqDiff = cpuFreqSnapshot.diff(lastCpuFreqSnapshot);
                            printer.createSubSection("cpufreq");
                            printer.writeLine("diff", Arrays.toString(cpuFreqDiff.dlt.cpuFreq));
                            printer.writeLine("curr", Arrays.toString(cpuFreqDiff.end.cpuFreq));
                        }
                        lastCpuFreqSnapshot = cpuFreqSnapshot;

                        // fixme: context config
                        BatteryTmpSnapshot batteryTmpSnapshot = deviceStatMonitor.currentBatteryTemperature(Matrix.with().getApplication());
                        if (lastBatteryTmpSnapshot != null) {
                            Delta<BatteryTmpSnapshot> batteryDiff = batteryTmpSnapshot.diff(lastBatteryTmpSnapshot);
                            printer.createSubSection("battery_tmp");
                            printer.writeLine("diff", String.valueOf(batteryDiff.dlt.temperature));
                            printer.writeLine("curr", String.valueOf(batteryDiff.end.temperature));
                        }
                        lastBatteryTmpSnapshot = batteryTmpSnapshot;
                    }
                });
            }
        }

        public StringBuilder getExtInfo() {
            return new StringBuilder();
        }

        protected void createSection(String sectionName, Consumer<Printer> printerConsumer) {
            mPrinter.createSection(sectionName);
            printerConsumer.accept(mPrinter);
        }

        /**
         * Log Printer
         */
        public static class Printer {
            private final StringBuilder sb = new StringBuilder("\t\n");

            public Printer() {
            }

            public Printer append(Object obj) {
                sb.append(obj);
                return this;
            }

            public Printer tab() {
                sb.append("\t");
                return this;
            }

            public Printer enter() {
                sb.append("\n");
                return this;
            }

            public Printer arrow() {
                sb.append("-> ");
                return this;
            }

            public Printer writeTitle() {
                sb.append("****************************************** PowerTest *****************************************").append("\n");
                return this;
            }

            public Printer createSection(String sectionName) {
                sb.append("+ --------------------------------------------------------------------------------------------").append("\n");
                sb.append("| ").append(sectionName).append(" :").append("\n");
                return this;
            }

            public Printer writeLine(String line) {
                sb.append("| ").append("  -> ").append(line).append("\n");
                return this;
            }

            public Printer writeLine(String key, String value) {
                sb.append("| ").append("  -> ").append(key).append("\t= ").append(value).append("\n");
                return this;
            }

            public Printer createSubSection(String name) {
                sb.append("| ").append("  <").append(name).append(">\n");
                return this;
            }

            public Printer writeEnding() {
                sb.append("**********************************************************************************************");
                return this;
            }

            public void clear() {
                sb.delete(0, sb.length());
            }

            public void dump() {
                MatrixLog.i(TAG, sb.toString());
            }

            @Override
            @NonNull
            public String toString() {
                return sb.toString();
            }
        }
    }
}
