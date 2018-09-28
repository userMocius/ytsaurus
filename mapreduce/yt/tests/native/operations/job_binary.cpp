#include <mapreduce/yt/tests/yt_unittest_lib/yt_unittest_lib.h>

#include <mapreduce/yt/interface/client.h>

#include <library/unittest/registar.h>

#include <util/generic/yexception.h>

#include <util/system/execpath.h>
#include <util/system/sysstat.h>
#include <util/system/tempfile.h>

#include <util/string/subst.h>

using namespace NYT;
using namespace NYT::NTesting;

////////////////////////////////////////////////////////////////////////////////

//
// We don't want our mapper to be launchable from original exe file.
// So we make two pretty random signatures.
// We consider binary to be 'good' if signatures matches, binary is 'bad' otherwise.
// Original binary is 'bad'.
// To fix binary we simply replace all occurrences of SIGNATURE1 with SIGNATURE2.
// It's important that signatures have same length.
char SIGNATURE1[] = "a4337bc45a8fc544c03f52dc550cd6e1e87021bc896588bd79e901e2";
char SIGNATURE2[] = "4d3dad6c7c120f14a57f35d1b36dce6f0ed9734127d5304585805d86";
static_assert(sizeof(SIGNATURE1) == sizeof(SIGNATURE2));

bool IsExecutableOk()
{
    return strcmp(SIGNATURE1, SIGNATURE2) == 0;
}

void WriteFixedExecutable(IOutputStream* out)
{
    auto programImage = TFileInput(GetExecPath()).ReadAll();
    SubstGlobal(programImage, SIGNATURE1, SIGNATURE2);
    out->Write(programImage);
}

void WriteFixedExecutable(const TString& outputPath)
{
    TFileOutput outf(outputPath);
    WriteFixedExecutable(&outf);
    outf.Finish();

    Chmod(~outputPath, MODE0755);
}

////////////////////////////////////////////////////////////////////////////////

class THackedFileKeyValueSwapper
    : public NYT::IMapper<
          NYT::TTableReader<NYT::TNode>,
          NYT::TTableWriter<NYT::TNode>>
{
public:
    void Do(TReader* reader, TWriter* writer) override
    {
        if (!IsExecutableOk()) {
            ythrow yexception() << "Bad executable sig1: " << SIGNATURE1 << " sig2: " << SIGNATURE2;
        }

        for (; reader->IsValid(); reader->Next()) {
            const auto& row = reader->GetRow();
            TNode result;
            result["key"] = row.At("value");
            result["value"] = row.At("key");

            writer->AddRow(result);
        }
    }
};
REGISTER_MAPPER(THackedFileKeyValueSwapper);

////////////////////////////////////////////////////////////////////////////////

const TVector<TNode> ExpectedOutput = {
    TNode()("key", "one")("value", "1"),
    TNode()("key", "five")("value", "5"),
    TNode()("key", "forty two")("value", "42"),
};

void WriteTestTable(IClientPtr client)
{
    auto writer = client->CreateTableWriter<TNode>("//testing/input");
    {
        writer->AddRow(TNode()("key", "1")("value", "one"));
        writer->AddRow(TNode()("key", "5")("value", "five"));
        writer->AddRow(TNode()("key", "42")("value", "forty two"));
        writer->Finish();
    }
}

////////////////////////////////////////////////////////////////////////////////

Y_UNIT_TEST_SUITE(JobBinary)
{
    Y_UNIT_TEST(VerifyMapperDoesntWorkFromOriginalBinary)
    {
        auto client = CreateTestClient();
        WriteTestTable(client);
        auto runMap = [&] {
            client->Map(
                TMapOperationSpec()
                .MaxFailedJobCount(1)
                .AddInput<TNode>("//testing/input")
                .AddOutput<TNode>("//testing/output"),
                new THackedFileKeyValueSwapper);
        };
        UNIT_ASSERT_EXCEPTION(runMap(), TOperationFailedError);
    }

    void JobBinaryLocalPath(bool enableLocalModeOptimization)
    {
        TConfigSaverGuard g;

        TConfig::Get()->EnableLocalModeOptimization = enableLocalModeOptimization;

        auto client = CreateTestClient();
        WriteTestTable(client);

        TTempFile fixedExecutable("fixed_executable");
        WriteFixedExecutable(fixedExecutable.Name());

        client->Map(
            TMapOperationSpec()
            .MaxFailedJobCount(1)
            .MapperSpec(TUserJobSpec().JobBinaryLocalPath(fixedExecutable.Name()))
            .AddInput<TNode>("//testing/input")
            .AddOutput<TNode>("//testing/output"),
            new THackedFileKeyValueSwapper);

        TVector<TNode> actual = ReadTable(client, "//testing/output");
        UNIT_ASSERT_VALUES_EQUAL(actual, ExpectedOutput);
    }

    Y_UNIT_TEST(JobBinaryLocalPath_LocalModeOn)
    {
        JobBinaryLocalPath(true);
    }

    Y_UNIT_TEST(JobBinaryLocalPath_LocalModeOff)
    {
        JobBinaryLocalPath(false);
    }

    void JobBinaryCypressPath(bool enableLocalModeOptimization)
    {
        TConfigSaverGuard g;

        TConfig::Get()->EnableLocalModeOptimization = enableLocalModeOptimization;

        auto client = CreateTestClient();
        WriteTestTable(client);

        {
            auto writer = client->CreateFileWriter(TRichYPath("//testing/fixed_executable").Executable(true));
            WriteFixedExecutable(writer.Get());
            writer->Finish();
        }

        client->Map(
            TMapOperationSpec()
            .MaxFailedJobCount(1)
            .MapperSpec(TUserJobSpec().JobBinaryCypressPath("//testing/fixed_executable"))
            .AddInput<TNode>("//testing/input")
            .AddOutput<TNode>("//testing/output"),
            new THackedFileKeyValueSwapper);

        TVector<TNode> actual = ReadTable(client, "//testing/output");
        UNIT_ASSERT_VALUES_EQUAL(actual, ExpectedOutput);
    }

    Y_UNIT_TEST(JobBinaryCypressPath_LocalModeOn)
    {
        JobBinaryLocalPath(true);
    }

    Y_UNIT_TEST(JobBinaryCypressPath_LocalModeOff)
    {
        JobBinaryLocalPath(false);
    }
}

////////////////////////////////////////////////////////////////////////////////
