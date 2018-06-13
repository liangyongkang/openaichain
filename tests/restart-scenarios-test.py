#!/usr/bin/env python3

import testUtils

import argparse
import random
import traceback

###############################################################
# Test for different nodes restart scenarios.
# Nodes can be producing or non-producing.
# -p <producing nodes count>
# -c <chain strategy[replay|resync|none]>
# -s <topology>
# -d <delay between nodes startup>
# -v <verbose logging>
# --kill-sig <kill signal [term|kill]>
# --kill-count <nodoac instances to kill>
# --dont-kill <Leave cluster running after test finishes>
# --dump-error-details <Upon error print etc/oac/node_*/config.ini and var/lib/node_*/stderr.log to stdout>
# --keep-logs <Don't delete var/lib/node_* folders upon test completion>
###############################################################


Print=testUtils.Utils.Print

def errorExit(msg="", errorCode=1):
    Print("ERROR:", msg)
    traceback.print_stack(limit=-1)
    exit(errorCode)

parser = argparse.ArgumentParser()
parser.add_argument("-p", type=int, help="producing nodes count", default=2)
parser.add_argument("-d", type=int, help="delay between nodes startup", default=1)
parser.add_argument("-s", type=str, help="topology", default="mesh")
parser.add_argument("-c", type=str, help="chain strategy[%s|%s|%s]" %
                    (testUtils.Utils.SyncResyncTag, testUtils.Utils.SyncNoneTag, testUtils.Utils.SyncHardReplayTag),
                    default=testUtils.Utils.SyncResyncTag)
parser.add_argument("--kill-sig", type=str, help="kill signal[%s|%s]" %
                    (testUtils.Utils.SigKillTag, testUtils.Utils.SigTermTag), default=testUtils.Utils.SigKillTag)
parser.add_argument("--kill-count", type=int, help="nodoac instances to kill", default=-1)
parser.add_argument("-v", help="verbose logging", action='store_true')
parser.add_argument("--leave-running", help="Leave cluster running after test finishes", action='store_true')
parser.add_argument("--dump-error-details",
                    help="Upon error print etc/oac/node_*/config.ini and var/lib/node_*/stderr.log to stdout",
                    action='store_true')
parser.add_argument("--keep-logs", help="Don't delete var/lib/node_* folders upon test completion",
                    action='store_true')
parser.add_argument("--clean-run", help="Kill all nodoac and kloac instances", action='store_true')
parser.add_argument("--p2p-plugin", help="select a p2p plugin to use (either net or bnet). Defaults to net.", default="net")

args = parser.parse_args()
pnodes=args.p
topo=args.s
delay=args.d
chainSyncStrategyStr=args.c
debug=args.v
total_nodes = pnodes
killCount=args.kill_count if args.kill_count > 0 else 1
killSignal=args.kill_sig
killEosInstances= not args.leave_running
dumpErrorDetails=args.dump_error_details
keepLogs=args.keep_logs
killAll=args.clean_run
p2pPlugin=args.p2p_plugin

seed=1
testUtils.Utils.Debug=debug
testSuccessful=False

assert (chainSyncStrategyStr == testUtils.Utils.SyncResyncTag or chainSyncStrategyStr == testUtils.Utils.SyncNoneTag or
        chainSyncStrategyStr == testUtils.Utils.SyncHardReplayTag)
random.seed(seed) # Use a fixed seed for repeatability.
cluster=testUtils.Cluster(walletd=True)
walletMgr=testUtils.WalletMgr(True)

try:
    cluster.setChainStrategy(chainSyncStrategyStr)
    cluster.setWalletMgr(walletMgr)

    cluster.killall(allInstances=killAll)
    cluster.cleanup()
    walletMgr.killall(allInstances=killAll)
    walletMgr.cleanup()

    Print ("producing nodes: %d, topology: %s, delay between nodes launch(seconds): %d, chain sync strategy: %s" % (
    pnodes, topo, delay, chainSyncStrategyStr))

    Print("Stand up cluster")
    if cluster.launch(pnodes, total_nodes, topo=topo, delay=delay, p2pPlugin=p2pPlugin) is False:
        errorExit("Failed to stand up oac cluster.")

    Print ("Wait for Cluster stabilization")
    # wait for cluster to start producing blocks
    if not cluster.waitOnClusterBlockNumSync(3):
        errorExit("Cluster never stabilized")

    Print("Stand up OAC wallet koacd")
    walletMgr.killall(allInstances=killAll)
    walletMgr.cleanup()
    if walletMgr.launch() is False:
        errorExit("Failed to stand up koacd.")

    accountsCount=total_nodes
    walletName="MyWallet"
    Print("Creating wallet %s if one doesn't already exist." % walletName)
    wallet=walletMgr.create(walletName)
    if wallet is None:
        errorExit("Failed to create wallet %s" % (walletName))

    Print ("Populate wallet with %d accounts." % (accountsCount))
    if not cluster.populateWallet(accountsCount, wallet):
        errorExit("Wallet initialization failed.")

    defproduceraAccount=cluster.defproduceraAccount
    oacAccount=cluster.oacAccount

    Print("Importing keys for account %s into wallet %s." % (defproduceraAccount.name, wallet.name))
    if not walletMgr.importKey(defproduceraAccount, wallet):
        errorExit("Failed to import key for account %s" % (defproduceraAccount.name))

    Print("Create accounts.")
    if not cluster.createAccounts(oacAccount):
        errorExit("Accounts creation failed.")

    Print("Wait on cluster sync.")
    if not cluster.waitOnClusterSync():
        errorExit("Cluster sync wait failed.")

    Print("Kill %d cluster node instances." % (killCount))
    if cluster.killSomeEosInstances(killCount, killSignal) is False:
        errorExit("Failed to kill Eos instances")
    Print("nodoac instances killed.")

    Print("Spread funds and validate")
    if not cluster.spreadFundsAndValidate(10):
        errorExit("Failed to spread and validate funds.")

    Print("Wait on cluster sync.")
    if not cluster.waitOnClusterSync():
        errorExit("Cluster sync wait failed.")

    Print ("Relaunch dead cluster nodes instances.")
    if cluster.relaunchEosInstances() is False:
        errorExit("Failed to relaunch Eos instances")
    Print("nodoac instances relaunched.")

    Print ("Resyncing cluster nodes.")
    if not cluster.waitOnClusterSync():
        errorExit("Cluster never synchronized")
    Print ("Cluster synched")

    Print("Spread funds and validate")
    if not cluster.spreadFundsAndValidate(10):
        errorExit("Failed to spread and validate funds.")

    Print("Wait on cluster sync.")
    if not cluster.waitOnClusterSync():
        errorExit("Cluster sync wait failed.")

    testSuccessful=True
finally:
    if testSuccessful:
        Print("Test succeeded.")
    else:
        Print("Test failed.")
    if not testSuccessful and dumpErrorDetails:
        cluster.dumpErrorDetails()
        walletMgr.dumpErrorDetails()
        Print("== Errors see above ==")

    if killEosInstances:
        Print("Shut down the cluster%s" % (" and cleanup." if (testSuccessful and not keepLogs) else "."))
        cluster.killall(allInstances=killAll)
        walletMgr.killall(allInstances=killAll)
        if testSuccessful and not keepLogs:
            Print("Cleanup cluster and wallet data.")
            cluster.cleanup()
            walletMgr.cleanup()

exit(0)
