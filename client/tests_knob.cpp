// Does a Knob actually MOVE when a human drags it?
//
// ⚠️ THIS EXISTS BECAUSE EVERY SLIDER IN THE PANEL WAS DEAD AND EVERY TEST WAS
// GREEN. Knob.qml bound the Slider's value to an expression containing the
// Slider's own value, so the first press formed a binding loop, QML broke the
// binding, and the control froze. qml_selftest loaded the QML without errors and
// --check-resolutions measured its geometry, because neither of them ever
// TOUCHES a control. A control no test presses is a control nobody has tested.
//
// So this one presses it, with synthetic mouse events, and asserts the moved()
// signal carries a value that is actually different.

#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QTest>
#include <cstdio>

static int failures = 0;

static void Check(const char* what, bool ok, const QString& detail = "") {
    std::printf("  %s %s%s\n", ok ? "ok  " : "FAIL", what,
                detail.isEmpty() ? "" : ("  " + detail).toStdString().c_str());
    if (!ok) ++failures;
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);

    QQuickView view;
    view.engine()->addImportPath(":/qml");
    // ⚠️ Size the ROOT ITEM to the view, and do it before show(). Without this the
    // root keeps its tiny implicit size, the window is 32x18, and every synthetic
    // click lands outside it - a test that fails for its own reasons tells you
    // nothing about the code under test.
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(300, 80);
    view.setSource(QUrl("qrc:/qml/HamDeck/Knob.qml"));
    if (view.status() != QQuickView::Ready) {
        std::printf("FAIL: Knob.qml did not load\n");
        for (const auto& e : view.errors()) std::printf("  %s\n", qPrintable(e.toString()));
        return 1;
    }
    auto* knob = view.rootObject();
    knob->setProperty("from", 0);
    knob->setProperty("to", 200);
    knob->setProperty("value", 100);
    view.show();
    QTest::qWaitForWindowExposed(&view);

    // The Slider inside the Knob.
    QQuickItem* slider = nullptr;
    for (QObject* o : knob->findChildren<QObject*>()) {
        if (QString(o->metaObject()->className()).contains("Slider")) {
            slider = qobject_cast<QQuickItem*>(o);
            if (slider) break;
        }
    }
    if (!slider) { std::printf("FAIL: no Slider inside Knob\n"); return 1; }

    Check("knob follows its value property when untouched",
          qRound(slider->property("value").toDouble()) == 100,
          QString("slider reads %1").arg(slider->property("value").toDouble()));

    std::printf("  [diag] slider geometry x=%.0f y=%.0f w=%.0f h=%.0f enabled=%d\n",
                slider->x(), slider->y(), slider->width(), slider->height(),
                slider->isEnabled() ? 1 : 0);
    std::printf("  [diag] view %dx%d  root w=%.0f h=%.0f\n", view.width(), view.height(),
                knob->width(), knob->height());

    QSignalSpy spy(knob, SIGNAL(moved(int)));

    // ⚠️ Drag it like a person: press, move, release. A press alone would not
    // form the loop that broke this, and setting value in code would not either -
    // which is precisely why the bug survived every existing test.
    const QPoint start = slider->mapToScene(QPointF(slider->width() * 0.5,
                                                    slider->height() * 0.5)).toPoint();
    const QPoint end   = slider->mapToScene(QPointF(slider->width() * 0.85,
                                                    slider->height() * 0.5)).toPoint();
    std::printf("  [diag] press at %d,%d  release at %d,%d\n", start.x(), start.y(), end.x(), end.y());
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, start);
    QTest::qWait(30);
    std::printf("  [diag] after press: pressed=%d value=%.1f\n",
                slider->property("pressed").toBool() ? 1 : 0,
                slider->property("value").toDouble());
    QTest::mouseMove(&view, start + QPoint(5, 0));
    QTest::mouseMove(&view, end);
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, end);
    QTest::qWait(100);
    std::printf("  [diag] after release: value=%.1f knob.value=%d\n",
                slider->property("value").toDouble(), knob->property("value").toInt());

    Check("dragging emits moved()", spy.count() > 0,
          QString("emitted %1 times").arg(spy.count()));
    if (spy.count() > 0) {
        const int got = spy.last().at(0).toInt();
        Check("moved() carries a NEW value, not the old one", got != 100,
              QString("moved(%1), started at 100").arg(got));
        Check("moved() value is in range", got >= 0 && got <= 200,
              QString("got %1").arg(got));
    }

    std::printf("%s\n", failures == 0 ? "all passed" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
