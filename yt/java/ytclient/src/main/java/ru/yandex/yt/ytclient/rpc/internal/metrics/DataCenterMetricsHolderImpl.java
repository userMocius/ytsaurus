package ru.yandex.yt.ytclient.rpc.internal.metrics;

import com.codahale.metrics.Histogram;
import com.codahale.metrics.MetricRegistry;
import com.codahale.metrics.SharedMetricRegistries;

import ru.yandex.yt.ytclient.rpc.DefaultRpcBusClient;

/**
 * @author dkondra
 */
public class DataCenterMetricsHolderImpl implements DataCenterMetricsHolder {
    public static final DataCenterMetricsHolderImpl INSTANCE = new DataCenterMetricsHolderImpl();
    private static final MetricRegistry METRICS = SharedMetricRegistries.getOrCreate("ytclient");

    @Override
    public double getDc99thPercentile(String dc) {
        Histogram pingHistogramDc = METRICS.histogram(MetricRegistry.name(DefaultRpcBusClient.class, "ping", dc));
        return pingHistogramDc.getSnapshot().get99thPercentile();
    }
}
