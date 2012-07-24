#!/bin/bash
################################################################################

function shout() {
    set +x
    echo '*' $@ >&2
    set -x
}

function tc() {
    set +x
    echo "##teamcity[$*]"
    set -x
}

function usage() {
    echo "$0: effortlessly build yt source tree on TeamCity build farm"
    echo "$0: <checkout-directory> <working-directory> <build-branch> <build-type> <with-package> <with-deploy>"
    echo ""
    echo "<build-type> have to be compliant with CMAKE_BUILD_TYPE"
    echo "<with-package> have to be either YES or NO"
    echo "<with-deploy> have to be either YES or NO"
    echo ""
    echo "Following environment variables must be set:"
    echo "  TEAMCITY_VERSION"
    echo "  TEAMCITY_BUILDCONF_NAME"
    echo "  TEAMCITY_PROJECT_NAME"
    echo "  BUILD_NUMBER"
    echo "  BUILD_VCS_NUMBER"
}

################################################################################
tc "progressMessage 'Setting up...'"

export LANG=en_US.UTF-8
export LANGUAGE=en_US.UTF-8
export LC_ALL=en_US.UTF-8
export LC_CTYPE=C

[[ -z "$TEAMCITY_VERSION"        ]] && usage && exit 1
[[ -z "$TEAMCITY_BUILDCONF_NAME" ]] && usage && exit 1
[[ -z "$TEAMCITY_PROJECT_NAME"   ]] && usage && exit 1

[[ -z "$BUILD_NUMBER" ]] && usage && exit 2
[[ -z "$BUILD_VCS_NUMBER" ]] && usage && exit 2

[[ -z "$1" || -z "$2" || -z "$3" || -z "$4" || -z "$5" || -z "$6" ]] && usage && exit 3

CHECKOUT_DIRECTORY=$1
WORKING_DIRECTORY=$2
BUILD_BRANCH=$3
BUILD_TYPE=$4
WITH_PACKAGE=$5
WITH_DEPLOY=$6

if [[ ( $WITH_PACKAGE != "YES" ) && ( $WITH_PACKAGE != "NO" ) ]]; then
    shout "WITH_PACKAGE have to be either YES or NO."
    exit 1
fi

if [[ ( $WITH_DEPLOY != "YES" ) && ( $WITH_DEPLOY != "NO" ) ]]; then
    shout "WITH_DEPLOY have to be either YES or NO."
    exit 1
fi

if [[ -z "$CC" ]]; then
    shout "C compiler is not specified; trying to find gcc-4.5..."
    CC=$(which gcc-4.5)
    shout "CC=$CC"
fi

if [[ -z "$CXX" ]]; then
    shout "C++ compiler is not specified; trying to find g++-4.5..."
    CXX=$(which g++-4.5)
    shout "CXX=$CXX"
fi

################################################################################

export CC
export CXX

export LC_ALL=C
export LANG=en_US.UTF-8

set -e
set -x

################################################################################

mkdir -p $WORKING_DIRECTORY

cd $WORKING_DIRECTORY

tc "blockOpened name='CMake'"

shout "Running CMake..."
tc "progressMessage 'Running CMake...'"
cmake \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
    -DCMAKE_COLOR_MAKEFILE:BOOL=OFF \
    -DYT_BUILD_ENABLE_EXPERIMENTS:BOOL=ON \
    -DYT_BUILD_ENABLE_TESTS:BOOL=ON \
    -DYT_BUILD_ENABLE_NODEJS:BOOL=ON \
    -DYT_BUILD_BRANCH=$BUILD_BRANCH \
    -DYT_BUILD_NUMBER=$BUILD_NUMBER \
    -DYT_BUILD_TAG=$(echo $BUILD_VCS_NUMBER | cut -c 1-7) \
    $CHECKOUT_DIRECTORY

trap '(cd $WORKING_DIRECTORY ; find . -name "default.log" -delete)' 0

tc "blockClosed name='CMake'"

tc "blockOpened name='make'"

shout "Running make (1/2; fast)..."
tc "progressMessage 'Running make (1/2; fast)...'"
make -j 8 >/dev/null 2>/dev/null || true

shout "Running make (2/2; slow)..."
tc "progressMessage 'Running make (2/2; slow)...'"
make -j 1

tc "blockClosed name='make'"

package_version=
package_ticket=

if [[ ( $WITH_PACKAGE = "YES" ) ]]; then
    tc "progressMessage 'Packing...'"

    make package
    make version

    package_version=$(cat ytversion)

    dupload --to common --nomail ARTIFACTS/yandex-yt*${package_version}*.changes

    tc "setParameter name='yt.package_built' value='1'"
    tc "setParameter name='yt.package_version' value='$package_version'"
fi

if [[ ( $WITH_PACKAGE = "YES" ) && ( $WITH_DEPLOY = "YES" ) ]]; then
    tc "progressMessage 'Deploying...'"

    comment_file=$(mktemp)
    deploy_file=ARTIFACTS/deploy_${package_version}

    # TODO(sandello): More verbose commentary is always better.
    # TODO(sandello): Insert proper buildTypeId here.

    trap 'rm -f $comment_file' INT TERM EXIT

    echo "Auto-generated ticket posted by $(hostname) on $(date)" > $comment_file
    echo "See http://teamcity.yandex.ru/viewLog.html?buildTypeId=bt1364&buildNumber=${BUILD_NUMBER}" >> $comment_file

    curl http://c.yandex-team.ru/auth_update/ticket_add/ \
        --silent --get \
        --data-urlencode "package[0]=yandex-yt" \
        --data-urlencode "version[0]=${package_version}" \
        --data-urlencode "package[1]=yandex-yt-http-api" \
        --data-urlencode "version[1]=${package_version}" \
        --data-urlencode "ticket[branch]=testing" \
        --data-urlencode "ticket[mailcc]=sandello@yandex-team.ru" \
        --data-urlencode "ticket[comment]@${comment_file}" \
        --cookie "conductor_auth=$(cat ~/.conductor_auth)" \
        --header "Accept: application/xml" \
        --write-out "\nHTTP %{http_code} (done in %{time_total}s)\n" \
        --output "${deploy_file}" \
        --show-error

    package_ticket=$(cat ${deploy_file} | grep URL | cut -d : -f 2- | cut -c 2-)

    tc "setParameter name='yt.package_ticket' value='$package_ticket'"
fi

set +e
a=0

tc "blockOpened name='Unit Tests'"

shout "Running unit tests..."
tc "progressMessage 'Running unit tests...'"

cd $WORKING_DIRECTORY
gdb \
    --batch \
    --return-child-result \
    --command=$CHECKOUT_DIRECTORY/scripts/teamcity-gdb-script \
    --args \
    ./bin/unittester \
        --gtest_color=no \
        --gtest_output=xml:$WORKING_DIRECTORY/unit_tests.xml
b=$?
a=$((a+b))

tc "blockClosed name='Unit Tests'"


# TODO(ignat): add correct installation for package "python-requests (>=0.13.1)"
# Unfortunetly python-requests of proper version is absent in debian repository.
# So I need to use easy_install.
# Now it is installed to teamcity manually.

ulimit -c unlimited

run_python_test()
{
    local dir=$1
    local test_name=$2
    local block_name="'${test_name} tests'"

    shout "Running $test_name tests..."

    tc "blockOpened name=${block_name}"
    tc "progressMessage 'Running $test_name tests...'"

    cd $dir
    PYTHONPATH="$CHECKOUT_DIRECTORY/python:$PYTHONPATH" \
    PATH="$WORKING_DIRECTORY/bin:$WORKING_DIRECTORY/yt/nodejs:$PATH" \
        py.test \
            -rx -v \
            --timeout 300 \
            --junitxml="$WORKING_DIRECTORY/test_${test_name}.xml"
    b=$?
    a=$((a+b))
    tc "blockClosed name=${block_name}"
}

run_python_test "$CHECKOUT_DIRECTORY/tests/integration" "integration"

if [ "$b" != "0" ]; then
    tmpdir="$HOME/failed_tests/$BUILD_VCS_NUMBER"
    mkdir -p "$tmpdir"

    shout "Integration tests failed, output was put to $tmpdir"
    cp -r $CHECKOUT_DIRECTORY/tests/integration/tests.sandbox/* "$tmpdir"
fi

cd "$CHECKOUT_DIRECTORY/python/yt_wrapper" && make
run_python_test "$CHECKOUT_DIRECTORY/python/yt_wrapper" "python_wrapper"

run_python_test "$CHECKOUT_DIRECTORY/python/yson" "python_yson"

tc "blockOpened name='JavaScript Tests'"

shout "Running JavaScript tests..."
tc "progressMessage 'Running JavaScript tests...'"

cd $WORKING_DIRECTORY/yt/nodejs
npm install && ./run_tests.sh -R xunit > $WORKING_DIRECTORY/test_javascript.xml
b=$?
a=$((a+b))

tc "blockClosed name='JavaScript Tests'"

cd $WORKING_DIRECTORY

# TODO(sandello): Export final package name as build parameter.
# TODO(sandello): Measure some statistics.
exit $a

