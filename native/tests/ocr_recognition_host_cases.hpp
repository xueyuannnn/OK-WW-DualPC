case rust::WorkflowOp::OcrImageCount:
out[0] = double(images.size());
break;
case rust::WorkflowOp::OcrIndexedImageSize: {
    const auto &im = images.at(size_t(a[0]));
    out[0] = im.cols;
    out[1] = im.rows;
    break;
}
case rust::WorkflowOp::OcrAppendImageIndex:
indices.push_back(size_t(a[0]));
break;
case rust::WorkflowOp::OcrAllocateRecognitionResults:
results.resize(size_t(a[0]));
break;
case rust::WorkflowOp::OcrHasCheckpoint:
out[0] = bool(checkpoint);
break;
case rust::WorkflowOp::Checkpoint:
checkpoint();
break;
case rust::WorkflowOp::OcrSortedImageSize: {
    const auto &im = images.at(indices.at(size_t(a[0])));
    out[0] = im.cols;
    out[1] = im.rows;
    break;
}
case rust::WorkflowOp::OcrAllocationFailure:
throw std::bad_alloc();
break;
case rust::WorkflowOp::OcrAllocateBatch:
batch = ov::Tensor(ov::element::f32, {size_t(a[0]), 3, 48, size_t(a[1])});
break;
case rust::WorkflowOp::OcrNormalizeBatchItem: {
    auto item = normalized(images.at(indices.at(size_t(a[0]))), 48, int(a[2]), false);
    memcpy(batch.data<float>() + (size_t(a[0]) - size_t(a[1])) * item.get_size(), item.data(),
           item.get_byte_size());
    break;
}
case rust::WorkflowOp::OcrSetRecognizerInput:
recRequest.set_input_tensor(batch);
break;
case rust::WorkflowOp::OcrInferRecognizer:
infer(recRequest, checkpoint);
break;
case rust::WorkflowOp::OcrReadRecognitionTensor:
output = recRequest.get_output_tensor();
shape = output.get_shape();
break;
case rust::WorkflowOp::OcrRecognitionShape:
out[0] = double(shape.size());
out[1] = shape.empty() ? 0 : double(shape[0]);
out[2] = shape.size() < 3 ? 0 : double(shape[2]);
break;
case rust::WorkflowOp::OcrAlphabetSize:
out[0] = double(alphabet.size());
break;
case rust::WorkflowOp::OcrRecognitionFloat:
out[0] = output.get_element_type() == ov::element::f32;
break;
case rust::WorkflowOp::OcrRecognitionSteps:
out[0] = double(shape[1]);
break;
case rust::WorkflowOp::OcrClearRecognizedText:
text = QString();
break;
case rust::WorkflowOp::OcrBestToken: {
    const auto row = output.data<float>() + (size_t(a[0]) * shape[1] + size_t(a[1])) * shape[2];
    // Keep the SDK extremum primitive's platform-specific
    // floating-point behavior; CTC state and timing live in Rust.
    const size_t index = size_t(std::max_element(row, row + shape[2]) - row);
    const float score = row[index];
    out[0] = double(index);
    out[1] = score;
    break;
}
case rust::WorkflowOp::OcrAppendToken:
text += alphabet.at(int(a[0]));
break;
case rust::WorkflowOp::OcrStoreRecognizedText:
results.at(indices.at(size_t(a[0]))) = {text, a[1]};
break;
case rust::WorkflowOp::OcrFailure:
fail(int(a[0]) == 4 ? "OCR 文本行过长" : "OCR 识别模型与字典不匹配");
break;
