import yaml
from pyspark import SparkConf
from pyspark.sql import SparkSession

from yt_spark_client.utils import *
from contextlib import contextmanager


@contextmanager
def spark_session(conf=SparkConf()):
    spark = SparkSession.builder.config(conf=conf).getOrCreate()
    try:
        yield spark
    finally:
        spark.stop()
        spark._jvm.ru.yandex.spark.yt.fs.YtClientProvider.close()


def create_spark_session(spark_id,
                         yt_cluster=None,
                         discovery_dir=None,
                         config_path=None,
                         app_name=None,
                         num_executors=1,
                         cores_per_executor=1,
                         mb_per_core=1024,
                         driver_memory_mb=None,
                         spark_conf_args=None):
    _NODES_COUNT = 5
    _MAX_CORES = 32
    _MAX_MEMORY = 64 * 1024
    _MIN_MEMORY = 512
    _MAX_EXECUTORS = _NODES_COUNT * 8
    _MAX_TOTAL_CORES = _MAX_CORES * _NODES_COUNT
    _MAX_TOTAL_MEMORY = _MAX_MEMORY * _NODES_COUNT

    _DEFAULT_NUM_EXECUTORS = 1
    _DEFAULT_CORES_PER_EXECUTOR = 1
    _DEFAULT_MB_PER_CORE = 1024

    if config_path is not None:
        with open(config_path) as f:
            config = yaml.load(f)
    else:
        config = {}

    yt_proxy = yt_cluster or config.get("yt_proxy", os.getenv("YT_PROXY"))
    yt_user = config.get("yt_user") or os.getenv("YT_USER") or os.getenv("USER")
    yt_token = config.get("yt_token") or os.getenv("YT_TOKEN") or default_token()
    yt_client = create_yt_client(yt_proxy, yt_token)
    discovery_dir = discovery_dir or config.get("discovery_dir") or default_discovery_dir(yt_user)
    log_dir = config.get("log_dir") or default_base_log_dir(discovery_dir)
    master = get_spark_master(spark_id, discovery_dir, rest=False, yt_client=yt_client)

    num_executors = num_executors or config.get("num_executors") or _DEFAULT_NUM_EXECUTORS
    cores_per_executor = cores_per_executor or config.get("cores_per_executor") or _DEFAULT_CORES_PER_EXECUTOR
    mb_per_core = mb_per_core or config.get("mb_per_core") or _DEFAULT_MB_PER_CORE
    driver_memory_mb = driver_memory_mb or config.get("driver_memory_mb") or mb_per_core

    if not isinstance(driver_memory_mb, int) or driver_memory_mb < _MIN_MEMORY or driver_memory_mb > _MAX_MEMORY:
        raise AssertionError("Invalid amount of driver memory")

    if not isinstance(num_executors, int) or num_executors < 1 or num_executors > _MAX_EXECUTORS:
        raise AssertionError("Invalid number of executors")

    if not isinstance(cores_per_executor, int) or cores_per_executor < 1 or cores_per_executor > _MAX_CORES:
        raise AssertionError("Invalid number of cores per executor")

    if not isinstance(mb_per_core, int) or mb_per_core < 1:
        raise AssertionError("Invalid amount of memory per core")

    executor_memory = mb_per_core * cores_per_executor

    if executor_memory > _MAX_MEMORY or executor_memory < _MIN_MEMORY:
        raise AssertionError("Invalid amount of memory per executor")

    if executor_memory * num_executors > _MAX_TOTAL_MEMORY:
        raise AssertionError("Invalid amount of total memory")

    if cores_per_executor * num_executors > _MAX_TOTAL_CORES:
        raise AssertionError("Invalid amount of total cores")

    app_name = app_name or "PySpark for {}".format(os.environ['USER'])

    conf = SparkConf()
    set_conf(conf, base_spark_conf(yt_proxy, yt_user, log_dir))
    set_conf(conf, default_dynamic_allocation_conf())
    set_conf(conf, spark_conf_args)
    conf.set("spark.hadoop.yt.token", yt_token)
    conf.set("spark.executor.instances", str(num_executors))
    conf.set("spark.executor.cores", str(cores_per_executor))
    conf.set("spark.executor.memory", "{}m".format(cores_per_executor * mb_per_core))
    conf.set("spark.driver.memory", "{}m".format(driver_memory_mb))

    return SparkSession.builder.config(conf=conf).appName(app_name).master(master).getOrCreate()
