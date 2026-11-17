#include "window.hpp"
#ifdef OKWW_NATIVE_ONLY
#include "runtime/template_store.hpp"
namespace okww {
namespace {
class RegionCanvas final : public QWidget {
    QImage image_;
    QRect selection_;
    QPoint begin_;
    bool dragging_ = false;
    QRectF displayed() const {
        QSizeF size(image_.size());
        size.scale(contentsRect().size(), Qt::KeepAspectRatio);
        return {QPointF((width() - size.width()) / 2, (height() - size.height()) / 2), size};
    }
    QPoint pixel(QPoint point) const {
        const auto r = displayed();
        return {std::clamp(qRound((point.x() - r.x()) * image_.width() / r.width()), 0, image_.width()),
                std::clamp(qRound((point.y() - r.y()) * image_.height() / r.height()), 0, image_.height())};
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor("#202020"));
        const auto r = displayed();
        painter.drawImage(r, image_);
        if (!selection_.isEmpty()) {
            painter.setPen(QPen(QColor("#00b7ff"), 2));
            const double sx = r.width() / image_.width(), sy = r.height() / image_.height();
            painter.drawRect(QRectF(r.x() + selection_.x() * sx, r.y() + selection_.y() * sy,
                                    selection_.width() * sx, selection_.height() * sy));
        }
    }
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() != Qt::LeftButton || !displayed().contains(event->position()))
            return;
        begin_ = pixel(event->position().toPoint());
        dragging_ = true;
        selection_ = {};
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if (!dragging_)
            return;
        const auto end = pixel(event->position().toPoint());
        selection_ = QRect(std::min(begin_.x(), end.x()), std::min(begin_.y(), end.y()),
                           std::abs(begin_.x() - end.x()), std::abs(begin_.y() - end.y()));
        update();
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        mouseMoveEvent(event);
        dragging_ = false;
        if (!selection_.isEmpty() && selected)
            selected(selection_);
    }

  public:
    std::function<void(QRect)> selected;
    explicit RegionCanvas(QImage image) : image_(std::move(image)) {
        setObjectName("templateCanvas");
        setMinimumSize(400, 225);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    void selection(QRect region) {
        selection_ = region;
        update();
    }
};
void markup(Window *parent, const QString &directory, const QJsonObject &source) {
    runtime::TemplateStore store(directory);
    QImage image(source["path"].toString());
    if (image.isNull())
        fail("无法读取模板图片");
    QDialog dialog(parent);
    dialog.setObjectName("templateMarkupDialog");
    auto layout = classic::dialogLayout(dialog, "模板标注",
        "在图片上拖动框选，点击“添加选区”。下方可精确调整位置和尺寸，保存后供脚本识别使用。", {1040, 800});
    auto canvas = new RegionCanvas(image);
    layout->addWidget(canvas, 1);
    auto table = new QTableWidget(0, 5);
    table->setObjectName("templateRegions");
    table->setHorizontalHeaderLabels({"模板名称", "横向位置", "纵向位置", "宽度", "高度"});
    table->verticalHeader()->hide();
    table->verticalHeader()->setDefaultSectionSize(48);
    table->setShowGrid(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < 5; ++column) {
        table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Fixed);
        table->setColumnWidth(column, 120);
    }
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setMinimumHeight(170);
    layout->addWidget(table);
    const auto addRow = [table, image](QString name, QRect box) {
        const int row = table->rowCount();
        table->insertRow(row);
        table->setItem(row, 0, new QTableWidgetItem(name));
        const int values[] = {box.x(), box.y(), box.width(), box.height()};
        for (int column = 1; column < 5; ++column) {
            auto spin = new QSpinBox;
            spin->setMinimumWidth(112);
            spin->setAlignment(Qt::AlignLeft);
            spin->setRange(column <= 2 ? 0 : 1, column == 1 || column == 3 ? image.width() : image.height());
            spin->setValue(values[column - 1]);
            table->setCellWidget(row, column, spin);
        }
        table->selectRow(row);
    };
    for (auto value : store.annotations(source["id"].toInt())) {
        const auto region = value.toObject();
        const auto box = region["bbox"].toArray();
        addRow(region["name"].toString(), {box[0].toInt(), box[1].toInt(), box[2].toInt(), box[3].toInt()});
    }
    auto pending = std::make_shared<QRect>();
    canvas->selected = [pending](QRect box) { *pending = box; };
    QObject::connect(table, &QTableWidget::itemSelectionChanged, &dialog, [table, canvas] {
        const int row = table->currentRow();
        if (row < 0)
            return;
        int values[4];
        for (int i = 0; i < 4; ++i)
            values[i] = qobject_cast<QSpinBox *>(table->cellWidget(row, i + 1))->value();
        canvas->selection({values[0], values[1], values[2], values[3]});
    });
    auto bar = new QHBoxLayout;
    auto add = new QPushButton("添加选区"), remove = new QPushButton("移除标注");
    add->setObjectName("templateAddRegion");
    remove->setObjectName("templateRemoveRegion");
    bar->addWidget(add);
    bar->addWidget(remove);
    bar->addStretch();
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText("保存");
    buttons->button(QDialogButtonBox::Save)->setProperty("primary", true);
    buttons->button(QDialogButtonBox::Cancel)->setText("取消");
    bar->addWidget(buttons);
    layout->addLayout(bar);
    QObject::connect(add, &QPushButton::clicked, &dialog, [&, pending] {
        if (pending->isEmpty()) {
            QMessageBox::information(&dialog, "模板标注", "请先在图片中拖动框选区域。");
            return;
        }
        addRow("template_" + QString::number(table->rowCount() + 1), *pending);
    });
    QObject::connect(remove, &QPushButton::clicked, &dialog, [table] {
        if (table->currentRow() >= 0)
            table->removeRow(table->currentRow());
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            QJsonArray regions;
            for (int row = 0; row < table->rowCount(); ++row) {
                QJsonArray box;
                for (int column = 1; column < 5; ++column)
                    box.append(qobject_cast<QSpinBox *>(table->cellWidget(row, column))->value());
                regions.append(QJsonObject{{"name", table->item(row, 0)->text()}, {"bbox", box}});
            }
            store.saveAnnotations(source["id"].toInt(), regions);
            dialog.accept();
        } catch (const std::exception &error) {
            QMessageBox::warning(&dialog, "标注未保存", errorText(error));
        }
    });
    dialog.exec();
}
} // namespace
QWidget *Window::templatesPage() {
    auto page = new QWidget;
    auto layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 16, 20, 16);
    auto heading = new QLabel("模板");
    heading->setObjectName("pageTitle");
    layout->addWidget(heading);
    auto bar = new QHBoxLayout;
    const auto button = [bar](QString text, QString name) {
        auto result = new QPushButton(text);
        result->setObjectName(name);
        bar->addWidget(result);
        return result;
    };
    auto screenshot = button("截图", "templateScreenshot");
    auto import = button("导入图片", "templateImportImage");
    auto edit = button("标注", "templateMarkup");
    auto remove = button("删除", "templateDelete");
    auto refresh = button("刷新", "templateRefresh");
    auto publish = button("保存到脚本资源", "templatePublish");
    bar->addStretch();
    layout->addLayout(bar);
    auto search = new QLineEdit;
    search->setObjectName("templateSearch");
    search->setPlaceholderText("按图片名称或模板名称搜索");
    layout->addWidget(search);
    auto grid = new QListWidget;
    grid->setObjectName("templateImages");
    grid->setViewMode(QListView::IconMode);
    grid->setResizeMode(QListView::Adjust);
    grid->setMovement(QListView::Static);
    grid->setIconSize({160, 100});
    grid->setGridSize({188, 150});
    grid->setWordWrap(true);
    layout->addWidget(grid, 1);
    auto caption =
        new QLabel("截图使用采集卡当前画面。双击图片可标注，模板随个人配置保存。相同名称不会覆盖内置模板。");
    caption->setObjectName("caption");
    caption->setWordWrap(true);
    layout->addWidget(caption);
    const auto folder = data_ + "/ok_templates";
    const auto load = [grid, search, folder, edit, remove] {
        const int previous =
            grid->currentItem() ? grid->currentItem()->data(Qt::UserRole).toJsonObject()["id"].toInt() : -1;
        grid->clear();
        try {
            for (auto value : runtime::TemplateStore(folder).images()) {
                const auto image = value.toObject();
                const auto title = image["file_name"].toString() + '\n' + image["labels"].toString();
                if (!title.contains(search->text(), Qt::CaseInsensitive))
                    continue;
                auto item = new QListWidgetItem(title, grid);
                QImage thumb(image["path"].toString());
                item->setIcon(QPixmap::fromImage(
                    thumb.scaled(160, 100, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
                item->setData(Qt::UserRole, image);
                item->setToolTip(title);
                if (image["id"].toInt() == previous)
                    grid->setCurrentItem(item);
            }
            edit->setEnabled(grid->currentItem());
            remove->setEnabled(grid->currentItem());
        } catch (const std::exception &error) {
            auto item = new QListWidgetItem(errorText(error), grid);
            item->setFlags(Qt::NoItemFlags);
        }
    };
    refreshTemplates_ = load;
    connect(refresh, &QPushButton::clicked, page, load);
    connect(publish, &QPushButton::clicked, page, [this, folder] {
        QDialog dialog(this);
        dialog.setObjectName("templatePublishDialog");
        auto layout = classic::dialogLayout(dialog, "保存模板资源",
            "将标注区域保存到脚本资源目录，导出脚本包时一起携带。保存后更新模板索引，原图保留。", {600, 340});
        auto names = new QCheckBox("同时生成模板名称枚举文件");
        names->setToolTip("生成 LabelEnum.py，供脚本通过枚举引用模板名称。");
        layout->addWidget(names);
        layout->addStretch();
        auto buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted)
            return;
        try {
            runtime::TemplateStore(folder).publish(data_ + "/ok_tasks/assets",
                                                   names->isChecked() ? data_ + "/ok_tasks/LabelEnum.py"
                                                                      : QString{});
            log("模板已保存到脚本资源，下次运行载入。");
        } catch (const std::exception &error) {
            QMessageBox::warning(this, "模板未保存", errorText(error));
        }
    });
    connect(search, &QLineEdit::textChanged, page, load);
    connect(grid, &QListWidget::itemSelectionChanged, page, [grid, edit, remove] {
        edit->setEnabled(grid->currentItem());
        remove->setEnabled(grid->currentItem());
    });
    const auto open = [this, folder, grid, load] {
        if (!grid->currentItem())
            return;
        try {
            markup(this, folder, grid->currentItem()->data(Qt::UserRole).toJsonObject());
            load();
        } catch (const std::exception &error) {
            QMessageBox::warning(this, "模板", errorText(error));
        }
    };
    connect(edit, &QPushButton::clicked, page, open);
    connect(grid, &QListWidget::itemDoubleClicked, page, open);
    connect(screenshot, &QPushButton::clicked, page, [this, folder, load] {
        try {
            const auto frame = backend_.latestFrame();
            if (!frame)
                fail("请先启动双机连接，等待采集画面就绪。");
            const auto picture = runtime::inferenceImage(*frame, config_);
            QImage image(picture.data, picture.cols, picture.rows, int(picture.step), QImage::Format_BGR888);
            runtime::TemplateStore(folder).add(image);
            load();
        } catch (const std::exception &error) {
            QMessageBox::warning(this, "截图未保存", errorText(error));
        }
    });
    connect(import, &QPushButton::clicked, page, [this, folder, load] {
        const auto path =
            QFileDialog::getOpenFileName(this, "导入模板图片", {}, "图片 (*.png *.jpg *.jpeg *.bmp)");
        if (path.isEmpty())
            return;
        try {
            runtime::TemplateStore(folder).add(QImage(path));
            load();
        } catch (const std::exception &error) {
            QMessageBox::warning(this, "图片未导入", errorText(error));
        }
    });
    connect(remove, &QPushButton::clicked, page, [this, grid, folder, load] {
        if (!grid->currentItem() ||
            QMessageBox::question(this, "删除模板",
                                  "删除所选图片及其标注？原图将保留在个人配置的模板回收目录。") !=
                QMessageBox::Yes)
            return;
        try {
            runtime::TemplateStore(folder).remove(
                grid->currentItem()->data(Qt::UserRole).toJsonObject()["id"].toInt());
            load();
        } catch (const std::exception &error) {
            QMessageBox::warning(this, "删除未完成", errorText(error));
        }
    });
    edit->setEnabled(false);
    remove->setEnabled(false);
    return page;
}
} // namespace okww
#endif
