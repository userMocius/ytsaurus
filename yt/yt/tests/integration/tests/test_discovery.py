from yt_env_setup import YTEnvSetup
from yt_commands import *  # noqa

##################################################################


class TestDiscovery(YTEnvSetup):
    NUM_MASTERS = 1
    NUM_SECONDARY_MASTER_CELLS = 1

    @authors("aleksandra-zh")
    def test_master_discovery_orchid(self):
        master = ls("//sys/primary_masters")[0]
        assert exists("//sys/primary_masters/{0}/orchid/discovery_server".format(master))

        assert exists("//sys/discovery/primary_master_cell")
        cell = ls("//sys/secondary_masters")[0]
        assert exists("//sys/discovery/secondary_master_cells/{0}".format(cell))


class TestDiscoveryServers(YTEnvSetup):
    NUM_DISCOVERY_SERVERS = 5

    @authors("aleksandra-zh")
    def test_discovery_servers(self):
        set("//sys/@config/security_manager/enable_distributed_throttler", True)

        create_user("u")
        set("//sys/users/u/@request_limits/read_request_rate/default", 1000)
        for _ in range(10):
            ls("//sys", authenticated_user="u")

    @authors("aleksandra-zh")
    def test_discovery_servers_orchid(self):
        primary_cell_tag = get("//sys/@primary_cell_tag")
        wait(lambda: exists("//sys/discovery_servers"))

        ds = ls(("//sys/discovery_servers"))[0]
        def primary_cell_tag_in_master_cells():
            master_cells = ls(("//sys/discovery_servers/{}/orchid/discovery_server/security/master_cells").format(ds))
            return str(primary_cell_tag) in master_cells
        wait(primary_cell_tag_in_master_cells)

        def enough_members():
            members = ls(("//sys/discovery_servers/{}/orchid/discovery_server/security/master_cells/{}/@members").format(ds, primary_cell_tag))
            return len(members) == self.NUM_MASTERS
        wait(enough_members)
