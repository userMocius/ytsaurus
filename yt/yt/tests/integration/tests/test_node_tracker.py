from yt_env_setup import YTEnvSetup, Restarter, NODES_SERVICE, MASTERS_SERVICE

from yt_commands import (
    authors, wait, execute_command, get_driver, build_snapshot,
    exists, ls, get, set, create, remove,
    read_table, write_table,
    create_rack, create_data_center, vanilla,
    set_node_banned)

import yt_error_codes

from yt.common import YtError

import pytest

from time import sleep

import shutil
import os

##################################################################


class TestNodeTracker(YTEnvSetup):
    NUM_MASTERS = 1
    NUM_NODES = 3
    NUM_SCHEDULERS = 1

    DELTA_NODE_CONFIG = {
        "tags": ["config_tag1", "config_tag2"],
        "exec_agent": {
            "slot_manager": {
                "slot_location_statistics_update_period": 100,
            },
        },
    }

    @authors("babenko")
    def test_ban(self):
        nodes = ls("//sys/cluster_nodes")
        assert len(nodes) == 3

        test_node = nodes[0]
        assert get("//sys/cluster_nodes/%s/@state" % test_node) == "online"

        set_node_banned(test_node, True)
        set_node_banned(test_node, False)

    @authors("babenko")
    def test_resource_limits_overrides_defaults(self):
        node = ls("//sys/cluster_nodes")[0]
        assert get("//sys/cluster_nodes/{0}/@resource_limits_overrides".format(node)) == {}

    @authors("psushin")
    def test_disable_write_sessions(self):
        nodes = ls("//sys/cluster_nodes")
        assert len(nodes) == 3

        create("table", "//tmp/t")
        write_table("//tmp/t", {"a": "b"})

        for node in nodes:
            set("//sys/cluster_nodes/{0}/@disable_write_sessions".format(node), True)

        def can_write():
            try:
                write_table("//tmp/t", {"a": "b"})
                return True
            except YtError:
                return False

        assert not can_write()

        for node in nodes:
            set("//sys/cluster_nodes/{0}/@disable_write_sessions".format(node), False)

        wait(lambda: can_write())

    @authors("psushin", "babenko")
    def test_disable_scheduler_jobs(self):
        nodes = ls("//sys/cluster_nodes")
        assert len(nodes) == 3

        test_node = nodes[0]
        assert get("//sys/cluster_nodes/{0}/@resource_limits/user_slots".format(test_node)) > 0
        set("//sys/cluster_nodes/{0}/@disable_scheduler_jobs".format(test_node), True)

        wait(lambda: get("//sys/cluster_nodes/{0}/@resource_limits/user_slots".format(test_node)) == 0)

    @authors("babenko")
    def test_resource_limits_overrides_valiation(self):
        node = ls("//sys/cluster_nodes")[0]
        with pytest.raises(YtError):
            remove("//sys/cluster_nodes/{0}/@resource_limits_overrides".format(node))

    @authors("babenko")
    def test_user_tags_validation(self):
        node = ls("//sys/cluster_nodes")[0]
        with pytest.raises(YtError):
            set("//sys/cluster_nodes/{0}/@user_tags".format(node), 123)

    @authors("babenko")
    def test_user_tags_update(self):
        node = ls("//sys/cluster_nodes")[0]
        set("//sys/cluster_nodes/{0}/@user_tags".format(node), ["user_tag"])
        assert get("//sys/cluster_nodes/{0}/@user_tags".format(node)) == ["user_tag"]
        assert "user_tag" in get("//sys/cluster_nodes/{0}/@tags".format(node))

    @authors("babenko")
    def test_config_tags(self):
        for node in ls("//sys/cluster_nodes"):
            tags = get("//sys/cluster_nodes/{0}/@tags".format(node))
            assert "config_tag1" in tags
            assert "config_tag2" in tags

    @authors("babenko")
    def test_rack_tags(self):
        create_rack("r")
        node = ls("//sys/cluster_nodes")[0]
        assert "r" not in get("//sys/cluster_nodes/{0}/@tags".format(node))
        set("//sys/cluster_nodes/{0}/@rack".format(node), "r")
        assert "r" in get("//sys/cluster_nodes/{0}/@tags".format(node))
        remove("//sys/cluster_nodes/{0}/@rack".format(node))
        assert "r" not in get("//sys/cluster_nodes/{0}/@tags".format(node))

    @authors("babenko", "shakurov")
    def test_create_cluster_node(self):
        kwargs = {"type": "cluster_node"}
        with pytest.raises(YtError):
            execute_command("create", kwargs)

    @authors("gritukan")
    def test_node_decommissioned(self):
        nodes = ls("//sys/cluster_nodes")
        assert len(nodes) == 3

        create("table", "//tmp/t")

        def can_write():
            try:
                write_table("//tmp/t", {"a": "b"})
                return True
            except YtError:
                return False

        for node in nodes:
            wait(lambda: get("//sys/cluster_nodes/{0}/@resource_limits/user_slots".format(node)) > 0)
        wait(lambda: can_write())

        for node in nodes:
            set("//sys/cluster_nodes/{0}/@decommissioned".format(node), True)

        for node in nodes:
            wait(lambda: get("//sys/cluster_nodes/{0}/@resource_limits/user_slots".format(node)) == 0)
        wait(lambda: not can_write())

        for node in nodes:
            set("//sys/cluster_nodes/{0}/@decommissioned".format(node), False)

        for node in nodes:
            wait(lambda: get("//sys/cluster_nodes/{0}/@resource_limits/user_slots".format(node)) > 0)
        wait(lambda: can_write())

    @authors("gritukan")
    @pytest.mark.skip("Per-slot location statistics can be heavy; enable after switching to new heartbeats")
    def test_slot_location_statistics(self):
        def get_max_slot_space_usage():
            max_slot_space_usage = -1
            for node in ls("//sys/cluster_nodes"):
                slot_locations_path = "//sys/cluster_nodes/{}/@statistics/slot_locations".format(node)
                statistics = get(slot_locations_path)
                for index in range(len(statistics)):
                    slot_space_usages = get(slot_locations_path + "/{}/slot_space_usages".format(index))
                    for slot_space_usage in slot_space_usages:
                        max_slot_space_usage = max(max_slot_space_usage, slot_space_usage)
            return max_slot_space_usage

        assert get_max_slot_space_usage() == 0
        op = vanilla(
            track=False,
            spec={
                "tasks": {
                    "my_task": {
                        "job_count": 1,
                        "command": "fallocate -l 10M my_file; sleep 3600",
                    }
                }
            }
        )

        wait(lambda: 10 * 1024 * 1024 <= get_max_slot_space_usage() <= 12 * 1024 * 1024)
        op.abort()

    @authors("akozhikhov")
    def test_memory_category_limits(self):
        node = ls("//sys/cluster_nodes")[0]

        memory_statistics = get("//sys/cluster_nodes/{}/@statistics/memory".format(node))
        assert "limit" not in memory_statistics["alloc_fragmentation"]
        assert memory_statistics["block_cache"]["limit"] == 2000000


##################################################################


class TestNodeTrackerMulticell(TestNodeTracker):
    NUM_SECONDARY_MASTER_CELLS = 2

    @authors("shakurov")
    def test_tag_replication(self):
        node = ls("//sys/cluster_nodes")[0]
        set("//sys/cluster_nodes/{0}/@user_tags/end".format(node), "cool_tag")

        create_rack("r0")
        create_data_center("d0")
        set("//sys/racks/r0/@data_center", "d0")
        set("//sys/cluster_nodes/{0}/@rack".format(node), "r0")

        tags = {tag for tag in get("//sys/cluster_nodes/{0}/@tags".format(node))}
        assert "cool_tag" in tags
        assert "r0" in tags
        assert "d0" in tags

        tags1 = {tag for tag in get("//sys/cluster_nodes/{0}/@tags".format(node), driver=get_driver(1))}
        assert tags1 == tags

        tags2 = {tag for tag in get("//sys/cluster_nodes/{0}/@tags".format(node), driver=get_driver(2))}
        assert tags2 == tags2


################################################################################

class TestNodeTrackerWithLegacyHeartbeats(TestNodeTracker):
    DELTA_NODE_CONFIG = {
        "tags": ["config_tag1", "config_tag2"],
        "use_new_heartbeats": False,
        "exec_agent": {
            "slot_manager": {
                "slot_location_statistics_update_period": 100,
            },
        },
    }


class TestNodeTrackerMulticellWithLegacyHeartbeats(TestNodeTracker):
    DELTA_NODE_CONFIG = {
        "tags": ["config_tag1", "config_tag2"],
        "use_new_heartbeats": False,
        "exec_agent": {
            "slot_manager": {
                "slot_location_statistics_update_period": 100,
            },
        },
    }

    NUM_SECONDARY_MASTER_CELLS = 2

################################################################################


class TestRemoveClusterNodes(YTEnvSetup):
    NUM_MASTERS = 1
    NUM_NODES = 3
    NUM_SECONDARY_MASTER_CELLS = 2

    DELTA_NODE_CONFIG = {
        "data_node": {
            "lease_transaction_timeout": 2000,
            "lease_transaction_ping_period": 1000,
            "register_timeout": 1000,
            "incremental_heartbeat_timeout": 1000,
            "full_heartbeat_timeout": 1000,
            "job_heartbeat_timeout": 1000,
        }
    }

    @authors("babenko")
    def test_remove_nodes(self):
        for _ in xrange(10):
            with Restarter(self.Env, NODES_SERVICE):
                for node in ls("//sys/cluster_nodes"):
                    wait(lambda: get("//sys/cluster_nodes/{}/@state".format(node)) == "offline")
                    id = get("//sys/cluster_nodes/{}/@id".format(node))
                    remove("//sys/cluster_nodes/" + node)
                    wait(lambda: not exists("#" + id))

            build_snapshot(cell_id=None)

            with Restarter(self.Env, MASTERS_SERVICE):
                pass


################################################################################


class TestNodesCreatedBanned(YTEnvSetup):
    NUM_MASTERS = 1
    NUM_NODES = 3
    DEFER_NODE_START = True

    @authors("shakurov")
    def test_new_nodes_created_banned(self):
        assert get("//sys/cluster_nodes/@count") == 0
        assert get("//sys/cluster_nodes/@online_node_count") == 0

        assert not get("//sys/@config/node_tracker/ban_new_nodes")
        set("//sys/@config/node_tracker/ban_new_nodes", True)

        self.Env.start_nodes(sync=False)

        wait(lambda: get("//sys/cluster_nodes/@count") == 3)
        wait(lambda: get("//sys/cluster_nodes/@online_node_count") == 0)

        nodes = ls("//sys/cluster_nodes")
        assert len(nodes) == 3

        for node in nodes:
            assert get("//sys/cluster_nodes/{0}/@banned".format(node))
            ban_message = get("//sys/cluster_nodes/{0}/@ban_message".format(node))
            assert "banned" in ban_message and "provisionally" in ban_message
            assert get("//sys/cluster_nodes/{0}/@state".format(node)) == "offline"

        for node in nodes:
            set("//sys/cluster_nodes/{0}/@banned".format(node), False)

        self.Env.synchronize()

        wait(lambda: get("//sys/cluster_nodes/@online_node_count") == 3)

        create("table", "//tmp/t")

        write_table("//tmp/t", {"a": "b"})
        read_table("//tmp/t")


class TestNodesCreatedBannedMulticell(TestNodesCreatedBanned):
    NUM_SECONDARY_MASTER_CELLS = 2


################################################################################


class TestNodeUnrecognizedOptionsAlert(YTEnvSetup):
    NUM_MASTERS = 1
    NUM_NODES = 1
    NUM_SCHEDULERS = 1

    DELTA_NODE_CONFIG = {
        "enable_unrecognized_options_alert": True,
        "some_nonexistent_option": 42,
    }

    @authors("gritukan")
    def test_node_unrecognized_options_alert(self):
        nodes = ls("//sys/cluster_nodes")
        alerts = get("//sys/cluster_nodes/{}/@alerts".format(nodes[0]))
        assert alerts[0]["code"] == yt_error_codes.UnrecognizedConfigOption


################################################################################


class TestReregisterNode(YTEnvSetup):
    NUM_NODES = 2
    FIRST_CONFIG = None
    SECOND_CONFIG = None

    @classmethod
    def _change_node_address(cls):
        path1 = os.path.join(cls.FIRST_CONFIG["data_node"]["store_locations"][0]["path"], "uuid")
        path2 = os.path.join(cls.SECOND_CONFIG["data_node"]["store_locations"][0]["path"], "uuid")

        with Restarter(cls.Env, NODES_SERVICE, sync=False):
            shutil.copy(path1, path2)

    @classmethod
    def modify_node_config(cls, config):
        if (cls.FIRST_CONFIG is None):
            cls.FIRST_CONFIG = config
        else:
            cls.SECOND_CONFIG = config

    @authors("aleksandra-zh")
    def test_reregister_node_with_different_address(self):
        TestReregisterNode._change_node_address()

        def get_online_node_count():
            online_count = 0
            for node in ls("//sys/cluster_nodes", attributes=["state"]):
                if node.attributes["state"] == "online":
                    online_count += 1
            return online_count

        wait(lambda: get_online_node_count() == 1)
        sleep(5)
        assert get_online_node_count() == 1
