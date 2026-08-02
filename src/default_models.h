#if !defined(LRC_DEFAULT_MODELS_H)
#define LRC_DEFAULT_MODELS_H

#if !defined(LRC_DEFAULT_MODEL_DIR)
#define LRC_DEFAULT_MODEL_DIR "models"
#endif

#define LRC_DEFAULT_VOCALS_MODEL_PATH \
    LRC_DEFAULT_MODEL_DIR "/UVR-MDX-NET-Voc_FT.onnx"
#define LRC_DEFAULT_CTC_MODEL_PATH \
    LRC_DEFAULT_MODEL_DIR "/mms-onnx/onnx/model.onnx"
#define LRC_DEFAULT_CTC_TOKENIZER_PATH \
    LRC_DEFAULT_MODEL_DIR "/mms-onnx/tokens.txt"

#endif /* LRC_DEFAULT_MODELS_H */
