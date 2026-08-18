#ifndef NEURALNET_ONNXMODELBUILDER_H_
#define NEURALNET_ONNXMODELBUILDER_H_

#include <string>
#include "../neuralnet/desc.h"
#include "../core/logger.h"

// Emits an ONNX ModelProto (serialized to bytes) describing a KataGo model, given its ModelDesc
// and the runtime board dimensions. The serialized bytes are intended to be handed to TensorRT's
// nvonnxparser, which builds the engine.
//
// The emitted graph reproduces KataGo's tensor semantics using NCHW inputs named
// InputMask / InputSpatial / InputGlobal /
// InputMeta, and RAW-head outputs named OutputPolicyPass / OutputPolicy / OutputValue /
// OutputScoreValue / OutputOwnership. Post-processing is intentionally left to the C++ getOutput
// code so that model loading, inference, and output decoding remain independent of ONNX emission.
//
// Weights are baked into the ModelProto as initializers, so the serialized bytes are fully
// self-contained.
namespace OnnxModelBuilder {
  struct Result {
    std::string serializedModel;  // the serialized ONNX ModelProto
  };

  // Build a serialized ONNX ModelProto for the given model.
  Result build(
    const ModelDesc& desc,
    int nnXLen,
    int nnYLen,
    bool requireExactNNLen,
    bool transformerNHWC,
    bool useFP16,
    Logger* logger
  );
}

#endif  // NEURALNET_ONNXMODELBUILDER_H_
