#include <QDebug>
#include <QColor>
#include <QSettings>
#include <QRegExpValidator>
#include <QMessageBox>

#include "DxWidget.h"
#include "ui_DxWidget.h"
#include "data/Data.h"
#include "DxFilterDialog.h"

int DxTableModel::rowCount(const QModelIndex&) const {
    return dxData.count();
}

int DxTableModel::columnCount(const QModelIndex&) const {
    return 8;
}

QVariant DxTableModel::data(const QModelIndex& index, int role) const {
    if (role == Qt::DisplayRole) {
        DxSpot spot = dxData.at(index.row());
        switch (index.column()) {
        case 0:
            return spot.time.toString();
        case 1:
            return spot.callsign;
        case 2:
            return QString::number(spot.freq, 'f', 4);
        case 3:
            return spot.mode;
        case 4:
            return spot.spotter;
        case 5:
            return spot.comment;
        case 6:
            return spot.dxcc.cont;
        case 7:
            return spot.dxcc_spotter.cont;
        default:
            return QVariant();
        }
    }
    else if (index.column() == 1 && role == Qt::BackgroundRole) {
        DxSpot spot = dxData.at(index.row());
        return Data::statusToColor(spot.status, QColor(Qt::white));
    }
    else if (index.column() == 1 && role == Qt::ToolTipRole) {
        DxSpot spot = dxData.at(index.row());
        return spot.dxcc.country + " [" + Data::statusToText(spot.status) + "]";
    }
    else if (index.column() == 1 && role == Qt::TextColorRole) {
        DxSpot spot = dxData.at(index.row());
        return Data::statusToInverseColor(spot.status, QColor(Qt::black));
    }

    return QVariant();
}

QVariant DxTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) return QVariant();

    switch (section) {
    case 0: return tr("Time");
    case 1: return tr("Callsign");
    case 2: return tr("Frequency");
    case 3: return tr("Mode");
    case 4: return tr("Spotter");
    case 5: return tr("Comment");
    case 6: return tr("Continent");
    case 7: return tr("Spotter Continent");

    default: return QVariant();
    }
}

void DxTableModel::addEntry(DxSpot entry) {
    beginInsertRows(QModelIndex(), dxData.count(), dxData.count());
    //dxData.append(entry);
    dxData.prepend(entry);
    endInsertRows();
}

QString DxTableModel::getCallsign(const QModelIndex& index) {
    return dxData.at(index.row()).callsign;
}

double DxTableModel::getFrequency(const QModelIndex& index) {
    return dxData.at(index.row()).freq;
}

void DxTableModel::clear() {
    beginResetModel();
    dxData.clear();
    endResetModel();
}

DXSpotFilterProxyModel::DXSpotFilterProxyModel(QObject* parent):
     QSortFilterProxyModel(parent)
{
    moderegexp.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
    contregexp.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
    spottercontregexp.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
}

void DXSpotFilterProxyModel::setModeFilterRegExp(const QString& regExp)
{
    moderegexp.setPattern(regExp);
    invalidateFilter();
}

void DXSpotFilterProxyModel::setContFilterRegExp(const QString& regExp)
{
    contregexp.setPattern(regExp);
    invalidateFilter();
}

void DXSpotFilterProxyModel::setSpotterContFilterRegExp(const QString &regExp)
{
    spottercontregexp.setPattern(regExp);
    invalidateFilter();
}

bool DXSpotFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    QModelIndex modeIndex= sourceModel()->index(sourceRow, 3, sourceParent);
    QModelIndex contIndex = sourceModel()->index(sourceRow, 6, sourceParent);
    QModelIndex spottercontIndex = sourceModel()->index(sourceRow, 7, sourceParent);

    QString mode = sourceModel()->data(modeIndex).toString();
    QString cont = sourceModel()->data(contIndex).toString();
    QString spottercont = sourceModel()->data(spottercontIndex).toString();

    return (mode.contains(moderegexp) && cont.contains(contregexp) && spottercont.contains(spottercontregexp));
}

bool DeleteHighlightedDXServerWhenDelPressedEventFilter::eventFilter(QObject *obj, QEvent *event)
{
    if ( event->type() == QEvent::KeyPress )
    {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key::Key_Delete /*&& keyEvent->modifiers() == Qt::ShiftModifier*/)
        {
            auto combobox = dynamic_cast<QComboBox *>(obj);
            if ( combobox )
            {
                combobox->removeItem(combobox->currentIndex());
                return true;
            }
        }
    }

    // standard event processing
    return QObject::eventFilter(obj, event);
}

DxWidget::DxWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::DxWidget) {

    QSettings settings;

    socket = nullptr;

    ui->setupUi(this);
    dxTableModel = new DxTableModel(this);

    proxyDXC = new DXSpotFilterProxyModel(this);
    proxyDXC->setSourceModel(dxTableModel);
    proxyDXC->setDynamicSortFilter(false);
    proxyDXC->setModeFilterRegExp(modeFilterRegExp());
    proxyDXC->setContFilterRegExp(contFilterRegExp());
    proxyDXC->setSpotterContFilterRegExp(spotterContFilterRegExp());

    ui->dxTable->setModel(proxyDXC);
    ui->dxTable->addAction(ui->actionFilter);
    ui->dxTable->hideColumn(6);  //continent
    ui->dxTable->hideColumn(7);  //spotter continent

    QStringList DXCservers = settings.value("dxc/servers", QStringList("hamqth.com:7300")).toStringList();
    ui->serverSelect->addItems(DXCservers);
    ui->serverSelect->installEventFilter(new DeleteHighlightedDXServerWhenDelPressedEventFilter);
    QRegExp rx("[^\\:]+:[0-9]{1,5}");
    ui->serverSelect->setValidator(new QRegExpValidator(rx,this));

}

void DxWidget::toggleConnect() {
    if (socket && socket->isOpen()) {
        disconnectCluster();

    }
    else {
        int pos = ui->serverSelect->currentIndex();
        QString curr_server = ui->serverSelect->currentText();
        QValidator::State state = ui->serverSelect->validator()->validate(curr_server,pos);

        if ( state != QValidator::Acceptable )
        {
            QMessageBox::warning(nullptr, QMessageBox::tr("DXC Server Name Error"),
                                          QMessageBox::tr("DXC Server address must be in format<p><b>hostname:port</b> (ex. hamqth.com:7300)</p>"));
            return;
        }
        connectCluster();
    }
}

void DxWidget::connectCluster() {
    QStringList server = ui->serverSelect->currentText().split(":");
    QString host = server[0];
    int port = server[1].toInt();

    socket = new QTcpSocket(this);

    connect(socket, SIGNAL(readyRead()), this, SLOT(receive()));
    connect(socket, SIGNAL(connected()), this, SLOT(connected()));
    connect(socket, SIGNAL(error(QAbstractSocket::SocketError)),
            this, SLOT(socketError(QAbstractSocket::SocketError)));

    ui->connectButton->setEnabled(false);
    ui->connectButton->setText(tr("Connecting..."));

    ui->log->clear();
    ui->dxTable->clearSelection();
    dxTableModel->clear();
    ui->dxTable->repaint();

    socket->connectToHost(host, port);
}

void DxWidget::disconnectCluster() {
    ui->sendButton->setEnabled(false);
    ui->connectButton->setEnabled(true);
    ui->connectButton->setText(tr("Connect"));

    socket->disconnect();
    socket->close();

    delete socket;
    socket = nullptr;
}

void DxWidget::saveDXCServers()
{
    QSettings settings;

    QStringList serversItems = getDXCServerList();
    settings.setValue("dxc/servers", serversItems);
}

QString DxWidget::modeFilterRegExp()
{
    QSettings settings;
    QString regexp = "NOTHING";

    if (settings.value("dxc/filter_mode_phone",true).toBool())   regexp = regexp + "|" + Data::MODE_PHONE;
    if (settings.value("dxc/filter_mode_cw",true).toBool())      regexp = regexp + "|" + Data::MODE_CW;
    if (settings.value("dxc/filter_mode_ft8",true).toBool())     regexp = regexp + "|" + Data::MODE_FT8;
    if (settings.value("dxc/filter_mode_digital",true).toBool()) regexp = regexp + "|" + Data::MODE_DIGITAL;

    return regexp;
}

QString DxWidget::contFilterRegExp()
{
    QSettings settings;
    return settings.value("dxc/filter_cont_regexp","NOTHING|AF|AN|AS|EU|NA|OC|SA").toString();
}

QString DxWidget::spotterContFilterRegExp()
{
    QSettings settings;
    return settings.value("dxc/filter_spotter_cont_regexp","NOTHING|AF|AN|AS|EU|NA|OC|SA").toString();
}

void DxWidget::send() {
    QByteArray data;
    data.append(ui->commandEdit->text());
    data.append("\r\n");

    socket->write(data);

    ui->commandEdit->clear();
}

void DxWidget::receive() {
    QSettings settings;
    QString data(socket->readAll());
    QStringList lines = data.split(QRegExp("(\a|\n|\r)+"));
    foreach (QString line, lines)
    {

        // Skip empty lines
        if ( line.length() == 0 )
        {
            continue;
        }

        if (line.startsWith("login") || line.contains(QRegExp("enter your call(sign)?:"))) {
            QByteArray call = settings.value("station/callsign").toByteArray();
            call.append("\r\n");
            socket->write(call);
        }

        if (line.startsWith("DX")) {
            int index = 0;

            QRegExp spotterRegExp("DX DE (([A-Z]|[0-9]|\\/)+):?", Qt::CaseInsensitive);
            index = spotterRegExp.indexIn(line, index);
            QString spotter = spotterRegExp.cap(1);
            index += spotter.size();

            QRegExp freqRegExp("([0-9]+\\.[0-9]+)");
            index = freqRegExp.indexIn(line, index);
            QString freq = freqRegExp.cap(1);
            index += freq.size();

            QRegExp callRegExp("(([A-Z]|[0-9]|\\/)+)");
            index = callRegExp.indexIn(line, index);
            QString call = callRegExp.cap(1);
            index += call.size();

            QRegExp commentRegExp(" (.*) ([0-9]{4})Z");
            index = commentRegExp.indexIn(line, index);
            QString comment = commentRegExp.cap(1).trimmed();
            QString time = commentRegExp.cap(2);

            DxccEntity dxcc = Data::instance()->lookupDxcc(call);
            DxccEntity dxcc_spotter = Data::instance()->lookupDxcc(spotter);

            QString country = dxcc.country;

            DxSpot spot;
            spot.time = QTime::currentTime();
            spot.callsign = call;
            spot.freq = freq.toDouble() / 1000;
            spot.band = Data::band(spot.freq).name;
            spot.mode = Data::freqToMode(spot.freq);
            spot.spotter = spotter;
            spot.comment = comment;
            spot.dxcc = dxcc;
            spot.dxcc_spotter = dxcc_spotter;
            spot.status = Data::dxccStatus(spot.dxcc.dxcc, spot.band, Data::freqToMode(spot.freq));

            emit newSpot(spot);

            dxTableModel->addEntry(spot);
            proxyDXC->invalidate();

            ui->dxTable->repaint();
        }

        ui->log->appendPlainText(line);
    }
}

void DxWidget::socketError(QAbstractSocket::SocketError socker_error) {

    QString error_msg = QObject::tr("Cannot connect to DXC Server <p>Reason <b>: ");
    switch (socker_error)
    {
    case QAbstractSocket::ConnectionRefusedError:
        error_msg.append(QObject::tr("Connection Refused"));
        break;
    case QAbstractSocket::RemoteHostClosedError:
        error_msg.append(QObject::tr("Host closed the connection"));
        break;
    case QAbstractSocket::HostNotFoundError:
        error_msg.append(QObject::tr("Host not found"));
        break;
    case QAbstractSocket::SocketTimeoutError:
        error_msg.append(QObject::tr("Timeout"));
        break;
    default:
        error_msg.append(QObject::tr("Internal Error"));

    }
    error_msg.append("</b></p>");

    qInfo() << "Detailed Error: " << socker_error;

    QMessageBox::warning(nullptr, QMessageBox::tr("DXC Server Connection Error"),
                                  error_msg);
    ui->sendButton->setEnabled(false);
    ui->connectButton->setEnabled(true);
    ui->connectButton->setText(tr("Connect"));
}

void DxWidget::connected() {

    ui->sendButton->setEnabled(true);
    ui->connectButton->setEnabled(true);
    ui->connectButton->setText(tr("Disconnect"));

    saveDXCServers();
}

void DxWidget::rawModeChanged() {
    if (ui->rawCheckBox->isChecked()) {
        ui->stack->setCurrentIndex(1);
    }
    else {
        ui->stack->setCurrentIndex(0);
    }
}

void DxWidget::entryDoubleClicked(QModelIndex index) {
    QString callsign = dxTableModel->getCallsign(index);
    double frequency = dxTableModel->getFrequency(index);
    emit tuneDx(callsign, frequency);
}

void DxWidget::actionFilter()
{
  DxFilterDialog dialog;

  if (dialog.exec() == QDialog::Accepted)
  {
      proxyDXC->setModeFilterRegExp(modeFilterRegExp());
      proxyDXC->setContFilterRegExp(contFilterRegExp());
      proxyDXC->setSpotterContFilterRegExp(spotterContFilterRegExp());
  }
}

QStringList DxWidget::getDXCServerList()
{
    QStringList ret;

    for ( int index = 0; index < ui->serverSelect->count(); index++ )
    {
        ret << ui->serverSelect->itemText(index);
    }
    return ret;
}

DxWidget::~DxWidget() {
    saveDXCServers();
    delete ui;
}
