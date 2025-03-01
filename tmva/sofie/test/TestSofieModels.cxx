//#include "TPython.h"
#include "TROOT.h"
#include "TSystem.h"
#include "TInterpreter.h"
//#include "TMacro.h"
#include <vector>
#include <fstream>
#include <limits>

#include "TMVA/RModel.hxx"
#include "TMVA/RModelParser_ONNX.hxx"

#include  "gtest/gtest.h"

bool verbose = true;
int sessionId = 0;

void ExecuteSofieParser(std::string modelName) {
   using namespace TMVA::Experimental;
   SOFIE::RModelParser_ONNX parser;
   std::string inputName = modelName + ".onnx";
   std::cout << "parsing file " << inputName << std::endl;
   SOFIE::RModel model = parser.Parse(inputName);
   std::cout << "generating model.....\n";
   model.Generate(1, 1);
   std::string outputName = modelName + ".hxx";
   std::cout << "writing model as header .....\n";
   model.OutputGenerated(); // outputName);
   std::cout << "output written in  " << outputName << std::endl;
}


int DeclareCode(std::string modelName)
{
   // increment session Id to avoid clash in session variable name
   sessionId++;
   // inference code for gInterpreter->Declare + gROOT->ProcessLine
   // one could also use TMacro build with correct signature
   // TMacro m("testSofie"); m.AddLine("std::vector<float> testSofie(float *x) { return s.infer(x);}")
   // std::vector<float> * result = (std::vector<float> *)m.Exec(Form(float*)0x%lx , xinput.data));
   std::string code = std::string("#include \"") + modelName + ".hxx\"\n";
   code += "TMVA_SOFIE_" + modelName + "::Session s" + std::to_string(sessionId) + ";\n";

   gInterpreter->Declare(code.c_str());
   return sessionId;  
}

std::vector<float> RunInference(float * x, int sId) {
   // run inference code using gROOT->ProcessLine
   printf("doing inference.....");
   TString cmd = TString::Format("s%d.infer( (float*)0x%lx )", sId,(ULong_t)x);
   if (!verbose)  cmd += ";";
   std::vector<float> *result = (std::vector<float> *)gROOT->ProcessLine(cmd);
   return *result;
}

void TestLinear(int nbatches, bool useBN = false, int inputSize = 10, int nlayers = 4)
{
   std::string modelName = "LinearModel";
   if (useBN) modelName += "_BN";
   modelName += "_B" + std::to_string(nbatches);

   // network parameters : nbatches, inputDim, nlayers
   std::vector<int> params = {nbatches, inputSize, nlayers};

   std::string command = "python LinearModelGenerator.py ";
   for (size_t i = 0; i < params.size(); i++)
      command += "  " + std::to_string(params[i]);
   if (useBN)
      command += "  --bn";

   printf("executing %s\n", command.c_str());
   gSystem->Exec(command.c_str());

   ExecuteSofieParser(modelName);

   int id = DeclareCode(modelName);

   // input data
   std::vector<float> xinput(nbatches * inputSize);
   for (int ib = 0; ib < nbatches; ib++) {
      std::vector<float> x1(inputSize, float(ib + 1));
      std::copy(x1.begin(), x1.end(), xinput.begin() + ib * inputSize);
   }

   auto result = RunInference(xinput.data(), id);

   // read reference value from test file
   std::vector<float> refValue(result.size());

   std::ifstream f(std::string(modelName + ".out").c_str());
   for (size_t i = 0; i < refValue.size(); ++i) {
      f >> refValue[i];
      if (verbose)
         std::cout << " result " << result.at(i) << " reference " << refValue[i] << std::endl;
      if (std::abs(refValue[i]) > 0.5)
         EXPECT_FLOAT_EQ(result.at(i), refValue[i]);
      else
         // expect float fails for small values
         EXPECT_NEAR(result.at(i), refValue[i], 10 * std::numeric_limits<float>::epsilon());
   }
}

void TestConv2D( int nbatches, bool useBN = false, int ngroups = 2, int nchannels = 2, int nd = 4, int nlayers = 4)
{
   std::string modelName = "Conv2dModel";
   if (useBN) modelName += "_BN";
   modelName += "_B" + std::to_string(nbatches);

   // input size is fixed to (nb, nc, nd, nd)
   
   const int inputSize = nchannels  * nd * nd;

   //const char *argv[5] = {}
   std::string argv[5];

   argv[0] = std::to_string(nbatches);
   //.c_str();
   argv[1] = std::to_string(nchannels);
   argv[2] = std::to_string(nd);
   argv[3] = std::to_string(ngroups);
   argv[4] = std::to_string(nlayers);
   std::string command = "python Conv2dModelGenerator.py ";
   for (int i = 0; i < 5; i++) {
      command += " ";
      command += argv[i];
   }
   if (useBN) command += "  --bn";
   printf("executing %s\n", command.c_str());
   gSystem->Exec(command.c_str());
   // TPython::ExecScript("Conv2dModelGenerator.py",5,argv);

  
   ExecuteSofieParser(modelName);

   int id = DeclareCode(modelName);

   // input data 
   std::vector<float> xinput(nbatches*inputSize);
   for (int ib = 0; ib < nbatches; ib++) {
      std::vector<float> x1(nd*nd, float(ib + 1));
      std::vector<float> x2(nd*nd, -float(ib + 1));
      // x1 and x2 are the two channels, if more channels will be with zero
      std::copy(x1.begin(), x1.end(), xinput.begin() + ib * inputSize);
      if (nchannels > 1)
         std::copy(x2.begin(), x2.end(), xinput.begin() + ib * inputSize + x1.size());
   }

   auto result = RunInference(xinput.data(), id);

   
   // read reference value from test file
   std::vector<float> refValue(result.size());

   std::ifstream f(std::string(modelName + ".out").c_str());
   for (size_t i = 0; i < refValue.size(); ++i) {
      f >> refValue[i];
      if (verbose) std::cout << " result " << result.at(i) << " reference " << refValue[i] << std::endl;
      if (std::abs(refValue[i]) > 0.5)
         EXPECT_FLOAT_EQ(result.at(i), refValue[i]);
      else 
         // expect float fails for small values
         EXPECT_NEAR(result.at(i), refValue[i], 10 * std::numeric_limits<float>::epsilon());
   }
}

TEST(SOFIE, Linear_B1) {
   TestLinear(1);
}
TEST(SOFIE, Linear_B4)
{
   // test batch =4 (equal output size)
   TestLinear(4);
}
TEST(SOFIE,Conv2d_B1) {
   TestConv2D(1);
}
TEST(SOFIE, Conv2d_B4)
{
   TestConv2D(4);
}
// test with batch normalization
TEST(SOFIE, Linear_BNORM_B4)
{
   TestLinear(8,true,5,4);
}
TEST(SOFIE, Conv2d_BNORM_B4)
{
   TestConv2D(5,true);
}