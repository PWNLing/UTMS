#include "mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUI();
}

MainWindow::~MainWindow() {}

void MainWindow::setupUI() {
    setWindowTitle(tr("GUET-UTMS"));
    resize(1400, 800);
}
