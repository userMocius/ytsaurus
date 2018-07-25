#!/usr/bin/env python
# -*- coding: utf-8 -*-

import argparse
import collections
import filecmp
import logging
import os
import re
import subprocess
import shutil
import sys
import tempfile

from xml.etree import ElementTree

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "git-svn"))
from git_svn_lib import (
    Git,
    Svn,
    init_git_svn,
    get_svn_url_for_git_svn_remote,
    fetch_git_svn,
    parse_git_svn_correspondence,
    pull_git_svn,
)

logger = logging.getLogger("Yt.GitSvn")

SCRIPT_PATH = os.path.dirname(os.path.realpath(__file__))
PROJECT_PATH = os.path.abspath(os.path.join(SCRIPT_PATH, ".."))

YP_GIT_PATH = "yp"
YP_GIT_PATHSPEC = ":/" + YP_GIT_PATH
YP_SVN_PATH = "yp"
ARCADIA_URL = "svn+ssh://arcadia.yandex.ru/arc/trunk/arcadia" 
YP_ARCADIA_URL = ARCADIA_URL + "/" + YP_SVN_PATH

YP_GIT_SVN_REMOTE_ID = "arcadia_yp"

class YpSyncError(RuntimeError):
    pass

def idented_lines(lines, ident_size=2):
    ident = " " * ident_size
    return "".join(ident + l + "\n" for l in lines)

# TODO (ermolovd): should use function `check_git_working_tree' from git_svn/ instead of this function
def git_verify_tree_clean(git, pathspec):
    status_output = git.call("status", "--porcelain", "--untracked-files=no", pathspec)
    if status_output:
        raise YpSyncError("git repository has unstaged changed:\n{}".format(status_output))

def git_verify_head_pushed(git):
    output = git.call("branch", "--remote", "--contains", "HEAD")
    if not output:
        raise YpSyncError("remote repo doesn't contain HEAD")

def verify_svn_tree_clean(svn, path):
    changed_item_list = [status for status in svn.iter_status(path) if status.status not in ("normal", "unversioned")]
    if changed_item_list:
        changed_files = idented_lines(["{} {}".format(s.status, s.relpath) for s in changed_item_list])
        raise YpSyncError("svn repository has unstaged changed:\n"
                          "{changed_files}\n"
                          "Use --ignore-svn-modifications to ignore them.\n".format(changed_files=changed_files))

def svn_get_last_modified_revision(url):
    svn = Svn()
    xml_svn_info = svn.call("info", "--xml", url)
    tree = ElementTree.fromstring(xml_svn_info)
    commit_lst = tree.findall("entry/commit")
    assert len(commit_lst) == 1
    return commit_lst[0].get("revision")

def verify_svn_revision_merged(git, git_svn_id, svn_revision):
    svn_url = get_svn_url_for_git_svn_remote(git, git_svn_id)
    git_log_pattern = "^git-svn-id: {}@{}".format(svn_url, svn_revision)
    log = git.call("log", "--grep", git_log_pattern)
    if not log.strip():
        raise YpSyncError("Svn revision {} is not merged to git.\n"
                          "Use --ignore-unmerged-svn-commits flag to skip this check.\n".format(svn_revision))

def verify_recent_svn_revision_merged(git, git_svn_id):
    svn_url = get_svn_url_for_git_svn_remote(git, git_svn_id)
    recent_revision = svn_get_last_modified_revision(svn_url)
    verify_svn_revision_merged(git, git_svn_id, recent_revision)

def rmrf(path):
    if os.path.exists(path):
        if os.path.isdir(path):
            shutil.rmtree(path)
        else:
            os.remove(path)

class LocalGit(object):
    def __init__(self, root):
        self.root = os.path.realpath(root)
        assert os.path.exists(os.path.join(self.root, '.git'))
        self.git = Git(root)

    def ls_files(self, pathspec):
        output = self.git.call("ls-files", "-z", "--full-name", pathspec)
        return output.strip('\000').split('\000')

    def abspath(self, path):
        return os.path.join(self.root, path)

    def rev_parse(self, rev="HEAD"):
	ret = self.git.call("rev-parse", rev).rstrip('\n')
	assert re.match('^[0-9a-f]{40}$', ret)
        return ret

def git_iter_files_to_sync(git, pathspec):
    for relpath in git.ls_files(pathspec):
        if os.path.islink(git.abspath(relpath)):
            warn = (
                "Skipping symlink file: `{}'\n"
                "(To be honest author of this script doubts that keeping symlink in a repo is a good idea.\n"
                "He encourages you to remove symlink file in order to mute this annoying warning.)").format(relpath)
            logger.warning(warn)
            continue
        yield relpath

class LocalSvn(object):
    def __init__(self, root):
        self.root = os.path.realpath(root)
        assert os.path.isdir(os.path.join(self.root, '.svn'))

    def iter_status(self, path):
        """
        possible statuses:
            - normal -- not changed
            - unversioned -- svn doesn't know about file
            - missing -- file removed localy but present in svn
            ...
        """
        SvnStatusEntry = collections.namedtuple("SvnStatusEntry", ["abspath", "relpath", "status"])

        path = os.path.join(self.root, path)
        xml_status = subprocess.check_output(['svn', 'status', '--xml', '--verbose', path])
        tree = ElementTree.fromstring(xml_status)
        for item in tree.findall("target/entry"):
            abspath = item.get("path")
            relpath = os.path.relpath(abspath, self.root)
            wc_status, = item.findall("wc-status")
            yield SvnStatusEntry(abspath, relpath, wc_status.get("item"))

    def abspath(self, path):
        return os.path.join(self.root, path)

    def revert(self, path):
        subprocess.check_call(["svn", "revert", "--recursive", self.abspath(path)])

    def add(self, path):
        if path.startswith('/') or not os.path.exists(self.abspath(path)):
            raise ValueError("Path '{}' must be relative to svn root".format(path))
        subprocess.check_call(["svn", "add", "--parents", self.abspath(path)])

    def remove(self, path):
        if path.startswith('/'):
            raise ValueError("Path '{}' must be relative to svn root".format(path))
        subprocess.check_call(["svn", "remove", self.abspath(path)])

def notify_svn(svn, base_path, rel_files):
    rel_files = frozenset(rel_files)

    svn_status = {}
    for item in svn.iter_status(base_path):
        svn_status[item.relpath] = item.status

    for relpath in sorted(rel_files):
        assert relpath.startswith(base_path + '/'), "Expected relpath '{}' to be inside directory: '{}'".format(relpath, base_path)
        status = svn_status.get(relpath, "unversioned")

        if status == "unversioned":
            svn.add(relpath)
        elif status not in ("normal", "modified"):
            raise RuntimeError("Unexpected svn status: '{}' for file '{}'".format(item.status, relpath))

    for relpath, status in svn_status.iteritems():
        if relpath in rel_files:
            continue
        if os.path.isdir(svn.abspath(relpath)):
            continue

        if status == "missing":
            svn.remove(relpath)
        else:
            raise RuntimeError, "Don't know what to do with file: '{}' status: '{}'".format(relpath, status)

def verify_svn_match_git(git, svn):
    git_rel_paths = set(git_iter_files_to_sync(git, YP_GIT_PATHSPEC))
    svn_status = list(svn.iter_status(YP_SVN_PATH))

    svn_tracked_rel_paths = set(item.relpath
                                for item in svn_status
                                if (
                                    item.status in ["normal", "modified", "added"]
                                    and not os.path.isdir(item.abspath)))

    only_in_git = git_rel_paths - svn_tracked_rel_paths
    only_in_svn = svn_tracked_rel_paths - git_rel_paths
    if only_in_git or only_in_svn:
        raise YpSyncError(
            "svn working copy doesn't match git repo\n"
            "files that are in git and not in svn:\n\n"
            "{only_in_git}\n"
            "files that are in svn and not in git:\n\n"
            "{only_in_svn}".format(
                only_in_git=idented_lines(only_in_git),
                only_in_svn=idented_lines(only_in_svn),
            ))

    diffed = []
    for relpath in git_rel_paths:
        svn_path = svn.abspath(relpath)
        git_path = git.abspath(relpath)
        if not filecmp.cmp(svn_path, git_path):
            diffed.append(relpath)
    if diffed:
        raise YpSyncError(
            "Some files in svn working copy differs from corresponding files from git repo:\n"
            "{diffed}\n".format(
                diffed=idented_lines(diffed)))

def subcommand_copy_to_local_svn(args):
    git = Git(PROJECT_PATH)
    local_git = LocalGit(PROJECT_PATH)
    svn = LocalSvn(args.arcadia)

    if args.check_unmerged_svn_commits:
        logger.info("check that svn doesn't have any commits that are not merged to github")
        verify_recent_svn_revision_merged(git, YP_GIT_SVN_REMOTE_ID)

    logger.info("check for local modifications in git repo")
    git_verify_tree_clean(git, YP_GIT_PATHSPEC)

    logger.info("check svn repository for local modifications")
    try:
        verify_svn_tree_clean(svn, YP_SVN_PATH)
    except YpSyncError:
        if not args.ignore_svn_modifications:
            raise
        svn.revert(YP_SVN_PATH)

    logger.info("copying files to arcadia directory")
    rmrf(svn.abspath(YP_SVN_PATH))
    # Copy files
    rel_files = list(git_iter_files_to_sync(local_git, YP_GIT_PATHSPEC))
    for rel_file in rel_files:
        git_file = local_git.abspath(rel_file)
        svn_file = svn.abspath(rel_file)

        svn_dir = os.path.dirname(svn_file)
        if not os.path.exists(svn_dir):
            os.makedirs(svn_dir)
        shutil.copy2(git_file, svn_file)

    logger.info("notify svn about changes")
    notify_svn(svn, YP_SVN_PATH, rel_files)

    logger.info("checking that HEAD is present at github")
    must_push_before_commit = False
    try:
        git_verify_head_pushed(git)
    except YpSyncError as e:
        must_push_before_commit = True

    print >>sys.stderr, (
        "====================================================\n"
        "YP has beed copied to svn working copy. Please go to\n"
        "  {arcadia_yp}\n"
        "and check that everything is ok. Once you are done run:\n"
        " $ {script} svn-commit --arcadia {arcadia}"
    ).format(
        arcadia=svn.abspath(""),
        arcadia_yp=svn.abspath(YP_SVN_PATH),
        script=sys.argv[0])

    if must_push_before_commit:
        print >>sys.stderr, "WARNING:", e
        print >>sys.stderr, "You can check for compileability but you will need to push changes to github before commit"


def subcommand_svn_commit(args):
    git = Git(PROJECT_PATH)
    local_git = LocalGit(PROJECT_PATH)
    svn = LocalSvn(args.arcadia)

    if args.check_unmerged_svn_commits:
        logger.info("check that svn doesn't have any commits that are not merged to github")
        verify_recent_svn_revision_merged(git, YP_GIT_SVN_REMOTE_ID)

    logger.info("check for local modifications in git repo")
    git_verify_tree_clean(git, YP_GIT_PATHSPEC)

    logger.info("checking that HEAD is present at github")
    git_verify_head_pushed(git)

    logger.info("comparing svn copy and git copy")
    verify_svn_match_git(local_git, svn)

    logger.info("prepare commit")
    head = local_git.rev_parse()
    fd, commit_message_file_name = tempfile.mkstemp("-yp-commit-message", text=True)
    print commit_message_file_name
    with os.fdopen(fd, 'w') as outf:
        outf.write(
            "Push {yp_path}/ to arcadia\n"
            "\n"
            "__BYPASS_CHECKS__\n"
            "yt:git_commit:{head}\n".format(
                yp_path=YP_SVN_PATH,
                head=head))
        if args.review:
            outf.write("\nREVIEW:new\n")

    print >>sys.stderr, "Commit is prepared, now run:\n"
    print >>sys.stderr, "$ svn commit {arcadia_yp_path} -F {commit_message_file_name}".format(
        arcadia_yp_path=svn.abspath(YP_SVN_PATH),
        commit_message_file_name=commit_message_file_name)

def subcommand_init(args):
    git = Git(PROJECT_PATH)
    init_git_svn(git, YP_GIT_SVN_REMOTE_ID, YP_ARCADIA_URL)

def subcommand_fetch(args):
    git = Git(PROJECT_PATH)
    svn = Svn()
    fetch_git_svn(git, svn, YP_GIT_SVN_REMOTE_ID, one_by_one=True)

def subcommand_pull(args):
    git = Git(PROJECT_PATH)
    svn = Svn()
    pull_git_svn(git, svn, YP_ARCADIA_URL, YP_GIT_SVN_REMOTE_ID, YP_GIT_PATH, YP_SVN_PATH,
                 revision=args.revision,
                 recent_push=args.recent_push)

def main():
    parser = argparse.ArgumentParser()

    subparsers = parser.add_subparsers()

    def add_arcadia_argument(p):
        p.add_argument("-a", "--arcadia", required=True, help="path to local svn working copy")

    def add_ignore_unmerged_svn_commits_argument(p):
        p.add_argument("--ignore-unmerged-svn-commits", dest="check_unmerged_svn_commits", default=True, action="store_false",
                       help="do not complain when svn has commits that are not merged into github")

    # copy-to-local-svn subcommand
    copy_to_local_svn_parser = subparsers.add_parser("copy-to-local-svn")
    copy_to_local_svn_parser.add_argument("--ignore-svn-modifications", default=False, action="store_true",
                                          help="ignore and override changes in svn working copy")
    add_arcadia_argument(copy_to_local_svn_parser)
    add_ignore_unmerged_svn_commits_argument(copy_to_local_svn_parser)
    copy_to_local_svn_parser.set_defaults(subcommand=subcommand_copy_to_local_svn)

    # local-svn-commit
    svn_commit_parser = subparsers.add_parser("svn-commit")
    add_arcadia_argument(svn_commit_parser)
    add_ignore_unmerged_svn_commits_argument(svn_commit_parser)
    svn_commit_parser.add_argument("--no-review", dest='review', default=True, action='store_false',
                                   help="do not create review, commit right away")
    svn_commit_parser.set_defaults(subcommand=subcommand_svn_commit)

    # init
    init_parser = subparsers.add_parser("init")
    init_parser.set_defaults(subcommand=subcommand_init)

    # fetch 
    fetch_parser = subparsers.add_parser("fetch")
    fetch_parser.set_defaults(subcommand=subcommand_fetch)

    # pull 
    pull_parser = subparsers.add_parser("pull")
    pull_parser.add_argument(
        "-r", "--revision",
        type=int,
        help="revision to merge (by default most recent revision will be merged)")
    pull_parser.add_argument(
        "--recent-push",
        metavar="<svn-revision>:<git-commit>",
        type=parse_git_svn_correspondence,
        help="recent push svn revision and corresponding git-commit (by default it is determined automatically)")
    pull_parser.set_defaults(subcommand=subcommand_pull)

    # Logging options
    logging_parser = parser.add_mutually_exclusive_group()
    logging_parser.add_argument(
        "-q", "--quiet", action="store_const", help="minimize logging",
        dest="log_level", const=logging.WARNING)
    logging_parser.add_argument(
        "-v", "--verbose", action="store_const", help="maximize logging",
        dest="log_level", const=logging.DEBUG)
    logging_parser.set_defaults(log_level=logging.INFO)

    args = parser.parse_args()

    # Set up logger
    logger.setLevel(args.log_level)
    handler = logging.StreamHandler()
    formatter = logging.Formatter("%(asctime)s - %(levelname)s - %(message)s")
    handler.setFormatter(formatter)
    logger.handlers.append(handler)

    # Run subcommand
    try:
        args.subcommand(args)
    except YpSyncError as e:
        print >>sys.stderr, "ERROR:", e
        print >>sys.stderr, "Error occurred exiting..."
        exit(1)

if __name__ == "__main__":
    main()
