#ifndef NEURALNET_ONNXMODELBUILDER_H_
#define NEURALNET_ONNXMODELBUILDER_H_

#include <string>
#include <vector>

#include "../neuralnet/desc.h"
#include "../core/logger.h"

// Emits an ONNX ModelProto (serialized to bytes) describing a KataGo model, given its ModelDesc
// and the runtime board dimensions. The serialized bytes are intended to be handed to TensorRT's
// nvonnxparser, which builds the engine.
//
// The emitted graph reproduces the same tensor semantics as the hand-assembled ModelParser in
// trtbackend.cpp: NCHW float32 I/O tensors, inputs named InputMask / InputSpatial / InputGlobal /
// InputMeta, and RAW-head outputs named OutputPolicyPass / OutputPolicy / OutputValue /
// OutputScoreValue / OutputOwnership. Post-processing is intentionally left to the C++ getOutput
// code, exactly as for the .bin.gz ModelParser path, so both paths share one decode path.
//
// Weights are baked into the ModelProto as initializers, so the serialized bytes are fully
// self-contained.
namespace OnnxModelBuilder {
  struct Result {
    std::string serializedModel;  // the serialized ONNX ModelProto

    // Names of numerically sensitive nodes. TensorRT 10 pins these layers to FP32; TensorRT 11
    // emits explicit Cast nodes around them in the strongly typed ONNX graph.
    std::vector<std::string> trunkTipAndHeadNodeNames;  // trunk-tip norm + policy head + value head
    std::vector<std::string> rmsNormNodeNames;          // every RMSNorm (transformer + trunk-tip) op
  };

  // Build a serialized ONNX ModelProto for the given model.
  Result build(
    const ModelDesc& desc,
    int nnXLen,
    int nnYLen,
    bool requireExactNNLen,
    bool transformerNHWC,
    bool explicitFP16,
    Logger* logger
  );
}

#endif  // NEURALNET_ONNXMODELBUILDER_H_
