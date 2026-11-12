#include <QtTest>
class ShowtimeTests : public QObject { Q_OBJECT private slots: void nothing() {} };
QTEST_MAIN(ShowtimeTests)
#include "showtime_tests.moc"
