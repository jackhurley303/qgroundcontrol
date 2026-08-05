/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include "QGCParameterDiffService.h"

/**
 * @class QGCParameterDiffServiceImpl
 * @brief Host implementation of "qgc.parameterDiff/1" over ParameterManager.
 *
 * The same file formats and the same comparison the host's own parameter
 * editor performs, behind a value-typed boundary: the parse produces plain
 * QVariantMap entries instead of QML-facing objects, and a failure is reported
 * in the result map instead of popping an application message — a host service
 * has no business raising UI on a plugin's behalf.
 *
 * Both calls are stateless. Nothing survives between the diff and the write,
 * so overlapping diffs (several files open at once) cannot interfere, and
 * writeParameterDiff() re-resolves every entry against the vehicle rather than
 * trusting what the caller passes back.
 */
class QGCParameterDiffServiceImpl : public QGCParameterDiffService
{
    Q_OBJECT

public:
    explicit QGCParameterDiffServiceImpl(QObject* parent = nullptr);

    QVariantMap diffParametersFromFile(int vehicleId, const QString& filePath) override;
    int writeParameterDiff(int vehicleId, const QVariantList& entries) override;
};
