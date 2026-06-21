#include <Seer/Magika/MagikaModel.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <vector>

using namespace Seer;

namespace
{

// Path to weights — set via cmake or assumes run from project root
constexpr auto kModelPath = "models/magika.weights";

std::vector<std::byte> toBytes(std::string_view s)
{
    std::vector<std::byte> v(s.size());
    for (size_t i = 0; i < s.size(); ++i)
        v[i] = static_cast<std::byte>(s[i]);
    return v;
}

} // namespace

TEST(MagikaModelTest, LoadWeights)
{
    auto result = MagikaModel::loadFromFile(kModelPath);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->numClasses(), 214u);
}

TEST(MagikaModelTest, ClassifyHtml)
{
    auto result = MagikaModel::loadFromFile(kModelPath);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    auto& model = *result;

    auto html = toBytes("<!DOCTYPE html><html><head><title>Test</title></head>"
                        "<body><h1>Hello World</h1></body></html>");
    auto pred = model.predict(html);

    // Should classify as html with reasonable confidence
    EXPECT_EQ(pred.label, "html") << "Got: " << pred.label << " (" << pred.confidence << ")";
    EXPECT_GT(pred.confidence, 0.3f);
}

TEST(MagikaModelTest, ClassifyJson)
{
    auto result = MagikaModel::loadFromFile(kModelPath);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    auto& model = *result;

    auto json = toBytes(R"({"name": "test", "version": 1, "items": [1, 2, 3]})");
    auto pred = model.predict(json);

    EXPECT_EQ(pred.label, "json") << "Got: " << pred.label << " (" << pred.confidence << ")";
    EXPECT_GT(pred.confidence, 0.3f);
}

TEST(MagikaModelTest, ClassifyPython)
{
    auto result = MagikaModel::loadFromFile(kModelPath);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    auto& model = *result;

    auto py = toBytes("#!/usr/bin/env python3\n"
                      "import sys\n"
                      "def main():\n"
                      "    print('hello world')\n"
                      "if __name__ == '__main__':\n"
                      "    main()\n");
    auto pred = model.predict(py);

    EXPECT_EQ(pred.label, "python") << "Got: " << pred.label << " (" << pred.confidence << ")";
    EXPECT_GT(pred.confidence, 0.3f);
}

TEST(MagikaModelTest, ClassifyXml)
{
    auto result = MagikaModel::loadFromFile(kModelPath);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    auto& model = *result;

    auto xml = toBytes("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                       "<root><item id=\"1\">Hello</item></root>");
    auto pred = model.predict(xml);

    EXPECT_EQ(pred.label, "xml") << "Got: " << pred.label << " (" << pred.confidence << ")";
    EXPECT_GT(pred.confidence, 0.3f);
}

TEST(MagikaModelTest, ClassifyShell)
{
    auto result = MagikaModel::loadFromFile(kModelPath);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    auto& model = *result;

    auto sh = toBytes("#!/bin/bash\n"
                      "set -euo pipefail\n"
                      "echo \"Hello from shell\"\n"
                      "for i in 1 2 3; do\n"
                      "    echo $i\n"
                      "done\n");
    auto pred = model.predict(sh);

    EXPECT_EQ(pred.label, "shell") << "Got: " << pred.label << " (" << pred.confidence << ")";
    EXPECT_GT(pred.confidence, 0.3f);
}

TEST(MagikaModelTest, ClassifyCpp)
{
    auto result = MagikaModel::loadFromFile(kModelPath);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    auto& model = *result;

    auto cpp = toBytes("#include <iostream>\n"
                       "#include <vector>\n\n"
                       "int main() {\n"
                       "    std::vector<int> v{1, 2, 3};\n"
                       "    for (auto x : v) {\n"
                       "        std::cout << x << std::endl;\n"
                       "    }\n"
                       "    return 0;\n"
                       "}\n");
    auto pred = model.predict(cpp);

    EXPECT_EQ(pred.label, "cpp") << "Got: " << pred.label << " (" << pred.confidence << ")";
    EXPECT_GT(pred.confidence, 0.3f);
}

TEST(MagikaModelTest, BadPathReturnsError)
{
    auto result = MagikaModel::loadFromFile("nonexistent.weights");
    EXPECT_FALSE(result.has_value());
}
