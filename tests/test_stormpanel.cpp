/**
 * @file test_stormpanel.cpp
 * @brief Widget-level acceptance of session-only frequency and storm evaluation.
 * @details Tests use owning synthetic values and the ordinary panel controls. No
 * network response, model snapshot, cache or temporary source file is created.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "StormPanel.hpp"
#include "NoaaClient.hpp"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QApplication>
#include <QStyleFactory>
#include <QStyle>
#include <QPixmap>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QToolButton>
#include <QMouseEvent>
#include <QDockWidget>
#include <QMainWindow>
#include <QTableView>
#include <QTest>
#include <cmath>

using namespace OpenSWMM::Data;
namespace
{
/** @brief Build a canonical synthetic source with dimensions and uncertainty bounds. */
FrequencyTable table()
{
    FrequencyTable result;
    result.durationsMinutes = {5., 60., 1440.};
    result.returnPeriods = {2., 10., 100.};
    result.depthsMm = {{2., 4., 6.}, {8., 16., 24.}, {25., 50., 75.}};
    result.lowerMm = {{1., 2., 3.}, {4., 8., 12.}, {12.5, 25., 37.5}};
    result.upperMm = {{3., 6., 9.}, {12., 24., 36.}, {37.5, 75., 112.5}};
    result.metadata = {{"volume", "2"}, {"version", "3"}, {"reg", "orb"}};
    result.retrievedAt = QDateTime::currentDateTimeUtc();
    return result;
}
/** @brief Select a deterministic manually entered triangular storm through public widgets. */
void manual(StormPanel &panel)
{
    panel.findChild<QComboBox *>("stormDepthSource")->setCurrentIndex(1);
    auto *method = panel.findChild<QComboBox *>("stormMethod");
    method->setCurrentIndex(method->findData("triangular"));
    panel.findChild<QDoubleSpinBox *>("stormTotalDepth")->setValue(30.);
    panel.findChild<QSpinBox *>("stormDurationMinutes")->setValue(60);
    panel.findChild<QSpinBox *>("stormIntervalMinutes")->setValue(5);
}
}
/** @brief Exercise actual widgets without relying on implementation-private model classes. */
class StormPanelTest final : public QObject
{
    Q_OBJECT
private slots:
    /** @brief Two independent docks see one accepted memory-only NOAA table. */
    void acceptedSourceIsSharedAcrossTools()
    {
        StormWorkspace workspace(nullptr);
        StormPanel frequency(nullptr, StormPanel::Role::Frequency, &workspace);
        StormPanel design(nullptr, StormPanel::Role::Design, &workspace);
        QString error;
        QVERIFY2(workspace.acceptFrequency(table(), &error), qPrintable(error));
        auto *values = frequency.findChild<QTableView *>("stormFrequencyTable");
        QCOMPARE(values->model()->rowCount(), 3);
        QCOMPARE(values->model()->columnCount(), 4);
        QCOMPARE(design.findChild<QComboBox *>("stormFrequency")->count(), 3);
        QVERIFY(design.findChild<QLabel *>("stormSourceCaption")->text().contains("38.9"));
        QVERIFY(!design.findChild<QPushButton *>("stormApply")->isEnabled());
    }
    /** @brief Manual candidate generation retains physical total and recording interval. */
    void manualGenerationConservesDepth()
    {
        StormWorkspace workspace(nullptr);
        StormPanel panel(nullptr, StormPanel::Role::Design, &workspace);
        manual(panel);
        panel.generate();
        QCOMPARE(panel.candidates().size(), 1);
        const auto candidate = panel.candidates().first();
        QCOMPARE(candidate.intervalMinutes, 5);
        QCOMPARE(candidate.depthsMm.size(), 12);
        QVERIFY(std::abs(candidate.totalDepthMm - 30.) < 1e-9);
        double total = 0.;
        for (double depth : candidate.depthsMm)
            total += depth;
        QVERIFY(std::abs(total - 30.) < 1e-9);
        QVERIFY(!panel.findChild<QPushButton *>("stormApply")->isEnabled());
    }
    /** @brief Display-unit changes preserve physical manual rainfall depth. */
    void switchingDisplayUnitsPreservesManualDepth()
    {
        StormWorkspace workspace(nullptr);
        StormPanel panel(nullptr, StormPanel::Role::Design, &workspace);
        manual(panel);
        panel.findChild<QDoubleSpinBox *>("stormTotalDepth")->setValue(25.4);
        panel.findChild<QComboBox *>("stormDisplayUnits")->setCurrentIndex(1);
        QCOMPARE(panel.findChild<QDoubleSpinBox *>("stormTotalDepth")->value(), 1.);
        panel.generate();
        QCOMPARE(panel.candidates().size(), 1);
        QVERIFY(std::abs(panel.candidates().first().totalDepthMm - 25.4) < 1e-9);
    }
    /** @brief Source confidence bounds are shown as separate numerical choices. */
    void uncertaintySelectionUsesRespectiveValues()
    {
        StormWorkspace workspace(nullptr);
        StormPanel panel(nullptr, StormPanel::Role::Frequency, &workspace);
        QVERIFY(workspace.acceptFrequency(table()));
        panel.findChild<QComboBox *>("stormFrequencyBound")->setCurrentIndex(1);
        auto *values = panel.findChild<QTableView *>("stormFrequencyTable");
        QCOMPARE(values->model()->index(2, 2).data().toString().toDouble(), 25.);
        panel.findChild<QComboBox *>("stormFrequencyBound")->setCurrentIndex(2);
        QCOMPARE(values->model()->index(2, 2).data().toString().toDouble(), 75.);
    }
    /** @brief Malformed structured values cannot replace a valid accepted source. */
    void incompleteResponseIsNotAdopted()
    {
        StormWorkspace workspace(nullptr);
        QVERIFY(workspace.acceptFrequency(table()));
        const auto revision = workspace.revision();
        auto invalid = table();
        invalid.lowerMm.clear();
        QString error;
        QVERIFY(!workspace.acceptFrequency(invalid, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(workspace.revision(), revision);
        QCOMPARE(workspace.frequency().lowerMm.size(), 3);
    }
    /** @brief A fixed gage interval cannot silently consume a shorter final bin. */
    void incompatibleIntervalDoesNotProduceCandidate()
    {
        StormWorkspace workspace(nullptr);
        StormPanel panel(nullptr, StormPanel::Role::Design, &workspace);
        manual(panel);
        panel.findChild<QSpinBox *>("stormIntervalMinutes")->setValue(7);
        panel.generate();
        QVERIFY(panel.candidates().isEmpty());
        QVERIFY(!panel.findChild<QLabel *>("stormStatus")->text().isEmpty());
    }
    /** @brief Custom cumulative knots are validated by the same core engine as standard patterns. */
    void customCurveUsesEnteredCumulativeValues()
    {
        StormWorkspace workspace(nullptr);
        StormPanel panel(nullptr, StormPanel::Role::Design, &workspace);
        manual(panel);
        auto *method = panel.findChild<QComboBox *>("stormMethod");
        method->setCurrentIndex(method->findData("custom"));
        panel.findChild<QPlainTextEdit *>("stormCustomCurve")->setPlainText("0,0\n0.5,0.2\n1,1");
        panel.generate();
        QCOMPARE(panel.candidates().size(), 1);
        QVERIFY(std::abs(panel.candidates().first().cumulativeMm[6] - 6.) < 1e-9);
        panel.findChild<QPlainTextEdit *>("stormCustomCurve")->setPlainText("0,0\n0.5,0.7\n1,0.3");
        panel.generate();
        QCOMPARE(panel.candidates().size(), 1); // Invalid input does not publish another overlay.
    }
    /** @brief A newly entered location cannot borrow the preceding point's depth table. */
    void editedLocationRequiresMatchingLiveSource()
    {
        StormWorkspace workspace(nullptr);
        StormPanel panel(nullptr, StormPanel::Role::Design, &workspace);
        QVERIFY(workspace.acceptFrequency(table()));
        panel.findChild<QDoubleSpinBox *>("stormLatitude")->setValue(42.);
        panel.generate();
        QVERIFY(panel.candidates().isEmpty());
        QVERIFY(panel.findChild<QLabel *>("stormStatus")->text().contains("currently entered"));
    }
    /** @brief Manual totals still need exact NOAA context for a frequency-derived shape. */
    void manualFrequencyShapeKeepsItsSourceReview()
    {
        StormWorkspace workspace(nullptr);
        StormPanel panel(nullptr, StormPanel::Role::Design, &workspace);
        QVERIFY(workspace.acceptFrequency(table()));
        manual(panel);
        auto *method = panel.findChild<QComboBox *>("stormMethod");
        method->setCurrentIndex(method->findData("alternating-block"));
        QVERIFY(panel.findChild<QComboBox *>("stormFrequency")->isEnabled());
        QVERIFY(panel.findChild<QComboBox *>("stormFrequencyBound")->isEnabled());
        panel.generate();
        QCOMPARE(panel.candidates().size(), 1);
        panel.findChild<QDoubleSpinBox *>("stormLatitude")->setValue(42.);
        panel.generate();
        QCOMPARE(panel.candidates().size(), 1); // Direct slot calls enforce the same source fence.
        QVERIFY(panel.findChild<QLabel *>("stormStatus")->text().contains("currently entered"));
    }
    /** @brief Edited temporal selectors cannot silently reuse another area's curve. */
    void temporalAreaChangeRetiresOldPatternChoice()
    {
        StormWorkspace workspace(nullptr);
        StormPanel panel(nullptr, StormPanel::Role::Design, &workspace);
        QVERIFY(workspace.acceptFrequency(table()));
        auto *method = panel.findChild<QComboBox *>("stormMethod");
        method->setCurrentIndex(method->findData("noaa-temporal"));
        CumulativeCurve curve{"orb/general/first/50%", "First quartile median", "Synthetic NOAA response", 1440.,
                               {{0., 0.}, {.5, .75}, {1., 1.}}};
        workspace.findChild<NoaaClient *>()->temporalReady({curve});
        QCOMPARE(panel.findChild<QComboBox *>("stormTemporalCurve")->count(), 1);
        panel.generate();
        QCOMPARE(panel.candidates().size(), 1);
        panel.findChild<QLineEdit *>("stormTemporalArea")->setText("another-area");
        QCOMPARE(panel.findChild<QComboBox *>("stormTemporalCurve")->count(), 0);
        panel.generate();
        QCOMPARE(panel.candidates().size(), 1);
        QVERIFY(panel.findChild<QLabel *>("stormStatus")->text().contains("temporal pattern"));
    }
    /** @brief Eight overlay candidates bound memory independently of source sizes. */
    void comparisonCandidatesAreBounded()
    {
        StormWorkspace workspace(nullptr);
        StormPanel panel(nullptr, StormPanel::Role::Design, &workspace);
        manual(panel);
        for (int index = 1; index <= 10; ++index) {
            panel.findChild<QDoubleSpinBox *>("stormTotalDepth")->setValue(index);
            panel.generate();
        }
        QCOMPARE(panel.candidates().size(), 8);
        QCOMPARE(panel.candidates().first().totalDepthMm, 3.);
        QCOMPARE(panel.candidates().last().totalDepthMm, 10.);
    }
    /** @brief Clear releases shared responses and visible candidate authority in both docks. */
    void clearReleasesSessionValues()
    {
        StormWorkspace workspace(nullptr);
        StormPanel frequency(nullptr, StormPanel::Role::Frequency, &workspace);
        StormPanel design(nullptr, StormPanel::Role::Design, &workspace);
        QVERIFY(workspace.acceptFrequency(table()));
        manual(design);
        design.generate();
        QVERIFY(!design.candidates().isEmpty());
        frequency.clear();
        QVERIFY(workspace.frequency().durationsMinutes.isEmpty());
        QVERIFY(design.candidates().isEmpty());
        QCOMPARE(frequency.findChild<QTableView *>("stormFrequencyTable")->model()->rowCount(), 0);
    }
    /** @brief Expanded source/target controls scroll inside a short dock without growing it. */
    void expandedSectionsStayWithinScrollableControls()
    {
        StormWorkspace workspace(nullptr);
        StormPanel panel(nullptr, StormPanel::Role::Design, &workspace);
        panel.resize(400, 500);
        panel.show();
        QApplication::processEvents();
        QCOMPARE(panel.size(), QSize(400, 500));
        auto *scroll = panel.findChild<QScrollArea *>("stormControlsScroll");
        QVERIFY2(scroll, "Tool inputs need their own scrolling region, independent of charts.");
        auto *source = panel.findChild<QToolButton *>("stormShowSource");
        auto *target = panel.findChild<QToolButton *>("stormShowTarget");
        auto *apply = panel.findChild<QPushButton *>("stormApply");
        const int collapsedRange = scroll->verticalScrollBar()->maximum();
        source->setChecked(true);
        target->setChecked(true);
        QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > collapsedRange);
        QCOMPARE(panel.size(), QSize(400, 500));
        QVERIFY(panel.minimumSizeHint().height() <= 260);
        QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        scroll->ensureWidgetVisible(apply, 0, 0);
        QTRY_VERIFY(scroll->viewport()->rect().contains(
            apply->mapTo(scroll->viewport(), apply->rect().center())));
        auto *chart = panel.findChild<QWidget *>("stormChart");
        QVERIFY(chart->isVisible());
        QVERIFY(chart->height() >= 100);
        source->setChecked(false);
        target->setChecked(false);
        QTRY_COMPARE(scroll->verticalScrollBar()->maximum(), collapsedRange);

        StormPanel frequency(nullptr, StormPanel::Role::Frequency, &workspace);
        frequency.resize(400, 500);
        frequency.show();
        QApplication::processEvents();
        QCOMPARE(frequency.size(), QSize(400, 500));
        auto *frequencyScroll = frequency.findChild<QScrollArea *>("stormControlsScroll");
        QVERIFY(frequencyScroll);
        QCOMPARE(frequencyScroll->horizontalScrollBar()->maximum(), 0);
        QVERIFY(frequency.findChild<QWidget *>("stormChart")->height() >= 100);
    }
    /** @brief Dragging the real splitter resizes charts and docking preserves evaluated values. */
    void splitterResizesCurvesAndRetainsEvaluation()
    {
        StormWorkspace workspace(nullptr);
        QMainWindow window;
        window.resize(460, 600);
        auto *dock = new QDockWidget(&window);
        auto *panel = new StormPanel(nullptr, StormPanel::Role::Design, &workspace);
        dock->setWidget(panel);
        window.addDockWidget(Qt::RightDockWidgetArea, dock);
        QVERIFY(workspace.acceptFrequency(table()));
        manual(*panel);
        panel->generate();
        const auto candidate = panel->candidates().first();
        auto *values = panel->findChild<QTableView *>("stormSampleTable");
        const int rows = values->model()->rowCount();
        QCOMPARE(rows, 12);
        window.show();
        QApplication::processEvents();
        auto *splitter = panel->findChild<QSplitter *>("stormLayoutSplitter");
        QVERIFY2(splitter, "Controls and charts must share a draggable vertical splitter.");
        QCOMPARE(splitter->orientation(), Qt::Vertical);
        auto *chart = panel->findChild<QWidget *>("stormChart");
        const int previousHeight = chart->height();
        auto *handle = splitter->handle(1);
        const QPoint press = handle->rect().center();
        QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, press);
        const QPoint moved = press - QPoint(0, 45);
        QMouseEvent drag(QEvent::MouseMove, moved, handle->mapToGlobal(moved),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(handle, &drag);
        QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, moved);
        QTRY_VERIFY(chart->height() >= previousHeight + 25);
        dock->setFloating(true);
        QApplication::processEvents();
        dock->setFloating(false);
        QApplication::processEvents();
        QCOMPARE(panel->candidates().size(), 1);
        QCOMPARE(panel->candidates().first().depthsMm, candidate.depthsMm);
        QCOMPARE(values->model()->rowCount(), rows);
        QCOMPARE(workspace.frequency().depthsMm, table().depthsMm);
    }
    /** @brief Save explicitly requested synthetic UI review artifacts in the verification folder. */
    void renderCompactToolsForReview()
    {
#ifdef SDK_STORM_VERIFICATION_DIRECTORY
        const QPalette previous = QApplication::palette();
        QPalette light = QApplication::style()->standardPalette();
        light.setColor(QPalette::Window, QColor("#f5f5f5"));
        light.setColor(QPalette::Base, Qt::white);
        light.setColor(QPalette::AlternateBase, QColor("#eeeeee"));
        light.setColor(QPalette::WindowText, Qt::black);
        light.setColor(QPalette::Text, Qt::black);
        light.setColor(QPalette::ButtonText, Qt::black);
        light.setColor(QPalette::Button, QColor("#eeeeee"));
        light.setColor(QPalette::Highlight, QColor("#2469ae"));
        light.setColor(QPalette::HighlightedText, Qt::white);
        QApplication::setPalette(light);
        const QString directory = QString::fromUtf8(SDK_STORM_VERIFICATION_DIRECTORY);
        QVERIFY(QDir().mkpath(directory));
        StormWorkspace workspace(nullptr);
        StormPanel frequency(nullptr, StormPanel::Role::Frequency, &workspace);
        StormPanel design(nullptr, StormPanel::Role::Design, &workspace);
        QVERIFY(workspace.acceptFrequency(table()));
        manual(design);
        design.findChild<QSpinBox *>("stormDurationMinutes")->setValue(1440);
        design.generate();
        auto *method = design.findChild<QComboBox *>("stormMethod");
        method->setCurrentIndex(method->findData("scs-ii"));
        QVERIFY(method->currentIndex() >= 0);
        design.generate();
        QCOMPARE(design.candidates().size(), 2);
        frequency.resize(500, 850);
        frequency.show();
        QApplication::processEvents();
        QVERIFY(frequency.grab().save(QDir(directory).filePath("frequency-tool.png")));
        frequency.hide();
        design.resize(500, 850);
        design.show();
        QApplication::processEvents();
        QVERIFY(design.grab().save(QDir(directory).filePath("design-storm-tool.png")));
        design.findChild<QComboBox *>("stormChartBasis")->setCurrentIndex(2);
        QVERIFY(design.grab().save(QDir(directory).filePath("design-storm-cumulative.png")));
        design.resize(400, 500);
        design.findChild<QToolButton *>("stormShowSource")->setChecked(true);
        design.findChild<QToolButton *>("stormShowTarget")->setChecked(true);
        auto *scroll = design.findChild<QScrollArea *>("stormControlsScroll");
        QApplication::processEvents();
        scroll->verticalScrollBar()->setValue(0);
        QVERIFY(design.grab().save(QDir(directory).filePath("design-storm-compact-expanded.png")));
        scroll->ensureWidgetVisible(design.findChild<QPushButton *>("stormApply"), 0, 0);
        QApplication::processEvents();
        QVERIFY(design.grab().save(QDir(directory).filePath("design-storm-compact-apply.png")));
        for (int index = 3; index <= 8; ++index) {
            design.findChild<QDoubleSpinBox *>("stormTotalDepth")->setValue(index * 10.);
            design.generate();
        }
        QCOMPARE(design.candidates().size(), 8);
        design.findChild<QSplitter *>("stormLayoutSplitter")->setSizes({10000, 0});
        QApplication::processEvents();
        const auto *compactChart = design.findChild<QWidget *>("stormChart");
        QVERIFY(compactChart->height() >= 100);
        QVERIFY(compactChart->height() <= 115);
        QVERIFY(design.grab().save(QDir(directory).filePath("design-storm-minimum-chart.png")));
        frequency.resize(400, 500);
        frequency.show();
        QApplication::processEvents();
        QVERIFY(frequency.grab().save(QDir(directory).filePath("frequency-tool-compact.png")));
        QApplication::setPalette(previous);
#else
        QSKIP("Set SDK_STORM_VERIFICATION_DIRECTORY for explicit synthetic UI review artifacts.");
#endif
    }
};
QTEST_MAIN(StormPanelTest)
#include "test_stormpanel.moc"
