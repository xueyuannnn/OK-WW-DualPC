
        std::vector<size_t> indices(images.size());
        std::iota(indices.begin(), indices.end(), 0);
        std::stable_sort(indices.begin(), indices.end(), [&](auto a, auto b) {
            return double(images[a].cols) / images[a].rows < double(images[b].cols) / images[b].rows;
        });
        std::vector<std::pair<QString, double>> results(images.size());
        for (size_t begin = 0; begin < indices.size(); begin += 6) {
            if (checkpoint) checkpoint();
            const size_t end = std::min(begin + 6, indices.size()), count = end - begin;
            double maxRatio = 320. / 48;
            for (size_t i = begin; i < end; ++i)
                maxRatio = std::max(maxRatio, double(images[indices[i]].cols) / images[indices[i]].rows);
            const int width = int(48 * maxRatio);
            if (width > 8192)
                fail("OCR 文本行过长");
            ov::Tensor batch(ov::element::f32, {count, 3, 48, size_t(width)});
            for (size_t i = begin; i < end; ++i) {
                auto item = normalized(images[indices[i]], 48, width, false);
                memcpy(batch.data<float>() + (i - begin) * item.get_size(), item.data(),
                       item.get_byte_size());
            }
            recRequest.set_input_tensor(batch);
            infer(recRequest, checkpoint);
            auto output = recRequest.get_output_tensor();
            auto shape = output.get_shape();
            if (shape.size() != 3 || shape[0] != count || shape[2] != size_t(alphabet.size()) ||
                output.get_element_type() != ov::element::f32)
                fail("OCR 识别模型与字典不匹配");
            auto data = output.data<float>();
            for (size_t i = 0; i < count; ++i) {
                QString text;
                double score = 0;
                int selected = 0, last = -1;
                for (size_t t = 0; t < shape[1]; ++t) {
                    if (checkpoint && t % 32 == 0) checkpoint();
                    auto row = data + (i * shape[1] + t) * shape[2];
                    int index = int(std::max_element(row, row + shape[2]) - row);
                    if (index && index != last) {
                        text += alphabet[index];
                        score += row[index];
                        ++selected;
                    }
                    last = index;
                }
                results[indices[begin + i]] = {text, selected ? score / selected : 0};
            }
        }
        return results;