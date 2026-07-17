#include "ui/GeofenceManagerWidget.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "core/GeofenceGeometry.h"

namespace utms {
namespace {

constexpr double kDefaultLatitude = 25.311724;
constexpr double kDefaultLongitude = 110.416819;

QDoubleSpinBox *createLatitudeSpinBox(QWidget *parent) {
    auto *spin_box = new QDoubleSpinBox(parent);
    spin_box->setRange(-90.0, 90.0);
    spin_box->setDecimals(7);
    spin_box->setSingleStep(0.0001);
    return spin_box;
}

QDoubleSpinBox *createLongitudeSpinBox(QWidget *parent) {
    auto *spin_box = new QDoubleSpinBox(parent);
    spin_box->setRange(-180.0, 180.0);
    spin_box->setDecimals(7);
    spin_box->setSingleStep(0.0001);
    return spin_box;
}

QString polygonText(const QVector<GeoPosition> &vertices) {
    QStringList lines;
    for (const GeoPosition &vertex : vertices) {
        lines.append(QStringLiteral("%1, %2").arg(vertex.latitude, 0, 'f', 7).arg(vertex.longitude, 0, 'f', 7));
    }
    return lines.join(QLatin1Char('\n'));
}

class GeofenceEditDialog : public QDialog {
public:
    GeofenceEditDialog(const std::optional<Geofence> &existing_geofence, const GeoPosition &default_center,
                       QWidget *parent)
        : QDialog(parent), existing_id_(existing_geofence.has_value() ? existing_geofence->id : 0) {
        setWindowTitle(existing_geofence.has_value() ? tr("编辑电子围栏") : tr("新建电子围栏"));
        resize(520, 500);
        setMinimumSize(480, 420);

        auto *layout = new QVBoxLayout(this);
        auto *basic_group = new QGroupBox(tr("基本信息"), this);
        auto *basic_layout = new QFormLayout(basic_group);
        name_line_edit_ = new QLineEdit(basic_group);
        shape_combo_box_ = new QComboBox(basic_group);
        shape_combo_box_->addItem(tr("圆形"), static_cast<int>(GeofenceShape::kCircle));
        shape_combo_box_->addItem(tr("矩形"), static_cast<int>(GeofenceShape::kRectangle));
        shape_combo_box_->addItem(tr("多边形"), static_cast<int>(GeofenceShape::kPolygon));
        enabled_check_box_ = new QCheckBox(tr("启用告警判断"), basic_group);
        visible_check_box_ = new QCheckBox(tr("在地图上显示"), basic_group);
        basic_layout->addRow(tr("名称"), name_line_edit_);
        basic_layout->addRow(tr("形状"), shape_combo_box_);
        basic_layout->addRow(enabled_check_box_);
        basic_layout->addRow(visible_check_box_);

        geometry_stack_ = new QStackedWidget(this);
        setupCirclePage(default_center);
        setupRectanglePage(default_center);
        setupPolygonPage(default_center);

        error_label_ = new QLabel(this);
        error_label_->setWordWrap(true);
        error_label_->setStyleSheet(QStringLiteral("QLabel { color: #c0392b; }"));
        error_label_->hide();
        auto *button_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(button_box, &QDialogButtonBox::accepted, this, &GeofenceEditDialog::accept);
        connect(button_box, &QDialogButtonBox::rejected, this, &GeofenceEditDialog::reject);
        connect(shape_combo_box_, qOverload<int>(&QComboBox::currentIndexChanged), geometry_stack_,
                &QStackedWidget::setCurrentIndex);

        layout->addWidget(basic_group);
        layout->addWidget(geometry_stack_, 1);
        layout->addWidget(error_label_);
        layout->addWidget(button_box);

        if (existing_geofence.has_value()) {
            applyGeofence(existing_geofence.value());
        } else {
            name_line_edit_->setText(tr("新建围栏"));
            enabled_check_box_->setChecked(true);
            visible_check_box_->setChecked(true);
        }
    }

    Geofence geofence() const { return geofence_; }

protected:
    void accept() override {
        Geofence candidate;
        candidate.id = existing_id_;
        candidate.name = name_line_edit_->text().trimmed();
        candidate.enabled = enabled_check_box_->isChecked();
        candidate.visible = visible_check_box_->isChecked();

        const auto shape = static_cast<GeofenceShape>(shape_combo_box_->currentData().toInt());
        if (shape == GeofenceShape::kCircle) {
            candidate.geometry =
                CircleGeofence{{circle_latitude_spin_box_->value(), circle_longitude_spin_box_->value()},
                               circle_radius_spin_box_->value()};
        } else if (shape == GeofenceShape::kRectangle) {
            candidate.geometry =
                RectangleGeofence{{south_latitude_spin_box_->value(), west_longitude_spin_box_->value()},
                                  {north_latitude_spin_box_->value(), east_longitude_spin_box_->value()}};
        } else {
            QVector<GeoPosition> vertices;
            const QStringList lines =
                polygon_plain_text_edit_->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            const QRegularExpression separator(QStringLiteral("[,，\\s]+"));
            for (qsizetype line_index = 0; line_index < lines.size(); ++line_index) {
                const QStringList values = lines.at(line_index).trimmed().split(separator, Qt::SkipEmptyParts);
                bool latitude_valid = false;
                bool longitude_valid = false;
                const double latitude = values.size() == 2 ? values.at(0).toDouble(&latitude_valid) : 0.0;
                const double longitude = values.size() == 2 ? values.at(1).toDouble(&longitude_valid) : 0.0;
                if (!latitude_valid || !longitude_valid) {
                    showError(tr("多边形第 %1 行格式错误，请输入“纬度, 经度”").arg(line_index + 1));
                    return;
                }
                vertices.append({latitude, longitude});
            }
            candidate.geometry = PolygonGeofence{vertices};
        }

        const QString validation_error = validateGeofence(candidate);
        if (!validation_error.isEmpty()) {
            showError(validation_error);
            return;
        }
        geofence_ = candidate;
        QDialog::accept();
    }

private:
    void setupCirclePage(const GeoPosition &center) {
        auto *page = new QWidget(geometry_stack_);
        auto *layout = new QFormLayout(page);
        circle_latitude_spin_box_ = createLatitudeSpinBox(page);
        circle_longitude_spin_box_ = createLongitudeSpinBox(page);
        circle_radius_spin_box_ = new QDoubleSpinBox(page);
        circle_radius_spin_box_->setRange(0.1, 1'000'000.0);
        circle_radius_spin_box_->setDecimals(1);
        circle_radius_spin_box_->setSuffix(tr(" m"));
        circle_latitude_spin_box_->setValue(center.latitude);
        circle_longitude_spin_box_->setValue(center.longitude);
        circle_radius_spin_box_->setValue(100.0);
        layout->addRow(tr("中心纬度"), circle_latitude_spin_box_);
        layout->addRow(tr("中心经度"), circle_longitude_spin_box_);
        layout->addRow(tr("半径"), circle_radius_spin_box_);
        geometry_stack_->addWidget(page);
    }

    void setupRectanglePage(const GeoPosition &center) {
        auto *page = new QWidget(geometry_stack_);
        auto *layout = new QFormLayout(page);
        south_latitude_spin_box_ = createLatitudeSpinBox(page);
        west_longitude_spin_box_ = createLongitudeSpinBox(page);
        north_latitude_spin_box_ = createLatitudeSpinBox(page);
        east_longitude_spin_box_ = createLongitudeSpinBox(page);
        south_latitude_spin_box_->setValue(center.latitude - 0.001);
        west_longitude_spin_box_->setValue(center.longitude - 0.001);
        north_latitude_spin_box_->setValue(center.latitude + 0.001);
        east_longitude_spin_box_->setValue(center.longitude + 0.001);
        layout->addRow(tr("西南角纬度"), south_latitude_spin_box_);
        layout->addRow(tr("西南角经度"), west_longitude_spin_box_);
        layout->addRow(tr("东北角纬度"), north_latitude_spin_box_);
        layout->addRow(tr("东北角经度"), east_longitude_spin_box_);
        geometry_stack_->addWidget(page);
    }

    void setupPolygonPage(const GeoPosition &center) {
        auto *page = new QWidget(geometry_stack_);
        auto *layout = new QVBoxLayout(page);
        auto *hint_label = new QLabel(tr("每行一个顶点，格式为“纬度, 经度”；按边界顺序输入 3–20 点。"), page);
        hint_label->setWordWrap(true);
        polygon_plain_text_edit_ = new QPlainTextEdit(page);
        polygon_plain_text_edit_->setPlainText(polygonText({{center.latitude - 0.001, center.longitude - 0.001},
                                                            {center.latitude + 0.001, center.longitude},
                                                            {center.latitude - 0.001, center.longitude + 0.001}}));
        layout->addWidget(hint_label);
        layout->addWidget(polygon_plain_text_edit_, 1);
        geometry_stack_->addWidget(page);
    }

    void applyGeofence(const Geofence &geofence) {
        name_line_edit_->setText(geofence.name);
        enabled_check_box_->setChecked(geofence.enabled);
        visible_check_box_->setChecked(geofence.visible);
        const GeofenceShape shape = geofenceShape(geofence);
        const int shape_index = shape_combo_box_->findData(static_cast<int>(shape));
        shape_combo_box_->setCurrentIndex(shape_index);
        if (const auto *circle = std::get_if<CircleGeofence>(&geofence.geometry); circle != nullptr) {
            circle_latitude_spin_box_->setValue(circle->center.latitude);
            circle_longitude_spin_box_->setValue(circle->center.longitude);
            circle_radius_spin_box_->setValue(circle->radius_m);
        } else if (const auto *rectangle = std::get_if<RectangleGeofence>(&geofence.geometry); rectangle != nullptr) {
            south_latitude_spin_box_->setValue(rectangle->southwest.latitude);
            west_longitude_spin_box_->setValue(rectangle->southwest.longitude);
            north_latitude_spin_box_->setValue(rectangle->northeast.latitude);
            east_longitude_spin_box_->setValue(rectangle->northeast.longitude);
        } else {
            polygon_plain_text_edit_->setPlainText(polygonText(std::get<PolygonGeofence>(geofence.geometry).vertices));
        }
    }

    void showError(const QString &message) {
        error_label_->setText(message);
        error_label_->show();
    }

    qint64 existing_id_ = 0;
    Geofence geofence_;
    QLineEdit *name_line_edit_ = nullptr;
    QComboBox *shape_combo_box_ = nullptr;
    QCheckBox *enabled_check_box_ = nullptr;
    QCheckBox *visible_check_box_ = nullptr;
    QStackedWidget *geometry_stack_ = nullptr;
    QDoubleSpinBox *circle_latitude_spin_box_ = nullptr;
    QDoubleSpinBox *circle_longitude_spin_box_ = nullptr;
    QDoubleSpinBox *circle_radius_spin_box_ = nullptr;
    QDoubleSpinBox *south_latitude_spin_box_ = nullptr;
    QDoubleSpinBox *west_longitude_spin_box_ = nullptr;
    QDoubleSpinBox *north_latitude_spin_box_ = nullptr;
    QDoubleSpinBox *east_longitude_spin_box_ = nullptr;
    QPlainTextEdit *polygon_plain_text_edit_ = nullptr;
    QLabel *error_label_ = nullptr;
};

} // namespace

GeofenceManagerWidget::GeofenceManagerWidget(QWidget *parent)
    : QWidget(parent),
      table_widget_(new QTableWidget(this)),
      create_button_(new QPushButton(tr("新建"), this)),
      edit_button_(new QPushButton(tr("编辑"), this)),
      locate_button_(new QPushButton(tr("定位"), this)),
      map_edit_button_(new QPushButton(tr("地图编辑"), this)),
      enabled_button_(new QPushButton(this)),
      visible_button_(new QPushButton(this)),
      delete_button_(new QPushButton(tr("删除"), this)),
      status_label_(new QLabel(tr("围栏数据库初始化中"), this)),
      map_edit_settlement_timer_(new QTimer(this)) {
    auto *layout = new QVBoxLayout(this);
    auto *button_layout = new QHBoxLayout();
    button_layout->addWidget(create_button_);
    button_layout->addWidget(edit_button_);
    button_layout->addWidget(locate_button_);
    button_layout->addWidget(map_edit_button_);
    button_layout->addWidget(enabled_button_);
    button_layout->addWidget(visible_button_);
    button_layout->addWidget(delete_button_);
    button_layout->addStretch();

    table_widget_->setColumnCount(4);
    table_widget_->setHorizontalHeaderLabels({tr("名称"), tr("形状"), tr("状态"), tr("地图显示")});
    table_widget_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_widget_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_widget_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_widget_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table_widget_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_widget_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_widget_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_widget_->setAlternatingRowColors(true);
    table_widget_->verticalHeader()->setVisible(false);

    status_label_->setWordWrap(true);
    layout->addLayout(button_layout);
    layout->addWidget(table_widget_, 1);
    layout->addWidget(status_label_);

    connect(create_button_, &QPushButton::clicked, this, &GeofenceManagerWidget::createGeofence);
    connect(edit_button_, &QPushButton::clicked, this, &GeofenceManagerWidget::editSelectedGeofence);
    connect(locate_button_, &QPushButton::clicked, this, &GeofenceManagerWidget::locateSelectedGeofence);
    connect(map_edit_button_, &QPushButton::clicked, this, &GeofenceManagerWidget::toggleMapEditing);
    connect(enabled_button_, &QPushButton::clicked, this, &GeofenceManagerWidget::toggleSelectedEnabled);
    connect(visible_button_, &QPushButton::clicked, this, &GeofenceManagerWidget::toggleSelectedVisible);
    connect(delete_button_, &QPushButton::clicked, this, &GeofenceManagerWidget::deleteSelectedGeofence);
    connect(table_widget_, &QTableWidget::itemSelectionChanged, this, &GeofenceManagerWidget::updateActions);
    connect(table_widget_, &QTableWidget::itemDoubleClicked, this,
            [this](QTableWidgetItem *) { editSelectedGeofence(); });
    map_edit_settlement_timer_->setSingleShot(true);
    connect(map_edit_settlement_timer_, &QTimer::timeout, this, [this]() {
        if (!editing_geofence_id_.has_value()) {
            map_edit_settling_ = false;
            updateActions();
        }
    });
    updateActions();
}

void GeofenceManagerWidget::setGeofences(const QVector<Geofence> &geofences) {
    const std::optional<Geofence> selected = selectedGeofence();
    geofences_ = geofences;
    table_widget_->setRowCount(geofences_.size());
    for (qsizetype row = 0; row < geofences_.size(); ++row) {
        const Geofence &geofence = geofences_.at(row);
        auto *name_item = new QTableWidgetItem(geofence.name);
        name_item->setData(Qt::UserRole, geofence.id);
        table_widget_->setItem(row, 0, name_item);
        table_widget_->setItem(row, 1, new QTableWidgetItem(geofenceShapeDisplayName(geofenceShape(geofence))));
        table_widget_->setItem(row, 2, new QTableWidgetItem(geofence.enabled ? tr("启用") : tr("禁用")));
        table_widget_->setItem(row, 3, new QTableWidgetItem(geofence.visible ? tr("显示") : tr("隐藏")));
        if (selected.has_value() && selected->id == geofence.id) {
            table_widget_->selectRow(row);
        }
    }
    showStatus(tr("共 %1 个电子围栏").arg(geofences_.size()), false);
    updateActions();
}

void GeofenceManagerWidget::applyMapEditedGeofence(const Geofence &geofence) {
    const auto current = std::find_if(geofences_.begin(), geofences_.end(),
                                      [&geofence](const Geofence &candidate) { return candidate.id == geofence.id; });
    if (current != geofences_.end()) {
        current->geometry = geofence.geometry;
    }
}

void GeofenceManagerWidget::setAvailable(bool available) {
    available_ = available;
    updateActions();
}

void GeofenceManagerWidget::setEditingGeofenceId(std::optional<qint64> geofence_id) {
    const bool was_editing = editing_geofence_id_.has_value();
    editing_geofence_id_ = geofence_id;
    if (geofence_id.has_value()) {
        map_edit_settlement_timer_->stop();
        map_edit_settling_ = false;
        showStatus(tr("地图编辑已开启：可拖动围栏、角点或顶点；修改后自动保存"), false);
    } else if (was_editing) {
        map_edit_settling_ = true;
        showStatus(tr("正在确认最后一次地图修改，请稍候…"), false);
        map_edit_settlement_timer_->start(2'000);
    }
    updateActions();
}

void GeofenceManagerWidget::showStatus(const QString &message, bool error) {
    status_label_->setText(message);
    status_label_->setStyleSheet(
        QStringLiteral("QLabel { color: %1; }").arg(error ? QStringLiteral("#c0392b") : QStringLiteral("#555555")));
}

std::optional<Geofence> GeofenceManagerWidget::selectedGeofence() const {
    const int row = table_widget_->currentRow();
    if (row < 0 || row >= table_widget_->rowCount() || table_widget_->item(row, 0) == nullptr) {
        return std::nullopt;
    }
    const qint64 geofence_id = table_widget_->item(row, 0)->data(Qt::UserRole).toLongLong();
    const auto geofence =
        std::find_if(geofences_.cbegin(), geofences_.cend(),
                     [geofence_id](const Geofence &candidate) { return candidate.id == geofence_id; });
    return geofence == geofences_.cend() ? std::nullopt : std::optional<Geofence>(*geofence);
}

void GeofenceManagerWidget::createGeofence() {
    GeofenceEditDialog dialog(std::nullopt, {kDefaultLatitude, kDefaultLongitude}, this);
    if (dialog.exec() == QDialog::Accepted) {
        emit createRequested(dialog.geofence());
    }
}

void GeofenceManagerWidget::editSelectedGeofence() {
    if (editing_geofence_id_.has_value() || map_edit_settling_) {
        showStatus(tr("请先结束地图编辑并等待最后一次修改保存完成"), true);
        return;
    }
    const std::optional<Geofence> selected = selectedGeofence();
    if (!selected.has_value()) {
        return;
    }
    GeofenceEditDialog dialog(selected, geofenceCenter(selected.value()), this);
    if (dialog.exec() == QDialog::Accepted) {
        emit updateRequested(dialog.geofence());
    }
}

void GeofenceManagerWidget::locateSelectedGeofence() {
    const std::optional<Geofence> selected = selectedGeofence();
    if (selected.has_value()) {
        emit locateRequested(selected->id);
    }
}

void GeofenceManagerWidget::toggleMapEditing() {
    if (map_edit_settling_) {
        showStatus(tr("正在确认最后一次地图修改，请稍候…"), true);
        return;
    }
    const std::optional<Geofence> selected = selectedGeofence();
    if (!selected.has_value()) {
        return;
    }
    if (!selected->visible) {
        showStatus(tr("请先显示该围栏，再开启地图编辑"), true);
        return;
    }
    emit mapEditRequested(editing_geofence_id_ == selected->id ? std::nullopt
                                                               : std::optional<qint64>(selected->id));
}

void GeofenceManagerWidget::toggleSelectedEnabled() {
    const std::optional<Geofence> selected = selectedGeofence();
    if (selected.has_value()) {
        emit enabledChangeRequested(selected->id, !selected->enabled);
    }
}

void GeofenceManagerWidget::toggleSelectedVisible() {
    const std::optional<Geofence> selected = selectedGeofence();
    if (selected.has_value()) {
        emit visibilityChangeRequested(selected->id, !selected->visible);
    }
}

void GeofenceManagerWidget::deleteSelectedGeofence() {
    const std::optional<Geofence> selected = selectedGeofence();
    if (!selected.has_value()) {
        return;
    }
    const QMessageBox::StandardButton choice = QMessageBox::question(
        this, tr("删除电子围栏"), tr("确定删除“%1”吗？已产生的告警历史不会被删除。").arg(selected->name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (choice == QMessageBox::Yes) {
        emit deleteRequested(selected->id);
    }
}

void GeofenceManagerWidget::updateActions() {
    const std::optional<Geofence> selected = selectedGeofence();
    create_button_->setEnabled(available_);
    edit_button_->setEnabled(available_ && selected.has_value() && !editing_geofence_id_.has_value() &&
                             !map_edit_settling_);
    locate_button_->setEnabled(selected.has_value());
    map_edit_button_->setEnabled(available_ && selected.has_value() && selected->visible && !map_edit_settling_);
    enabled_button_->setEnabled(available_ && selected.has_value());
    visible_button_->setEnabled(available_ && selected.has_value());
    delete_button_->setEnabled(available_ && selected.has_value());
    enabled_button_->setText(selected.has_value() && selected->enabled ? tr("禁用") : tr("启用"));
    visible_button_->setText(selected.has_value() && selected->visible ? tr("隐藏") : tr("显示"));
    map_edit_button_->setText(selected.has_value() && editing_geofence_id_ == selected->id ? tr("结束地图编辑")
                                                                                           : tr("地图编辑"));
}

} // namespace utms
