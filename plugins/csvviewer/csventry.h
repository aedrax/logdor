#ifndef CSVENTRY_H
#define CSVENTRY_H

#include <QString>
#include <QStringList>
#include <QVariant>
#include "../../app/src/plugininterface.h"

struct CsvEntry {
    QStringList values;
    
    CsvEntry() = default;
    
    explicit CsvEntry(const LogEntry& entry) {
        QString line = entry.getMessage();
        // Split by comma, handling quoted values
        bool inQuotes = false;
        QString currentValue;
        
        for (int i = 0; i < line.length(); ++i) {
            QChar c = line[i];
            
            if (c == '"') {
                inQuotes = !inQuotes;
            } else if (c == ',' && !inQuotes) {
                values.append(currentValue.trimmed());
                currentValue.clear();
            } else {
                currentValue.append(c);
            }
        }
        
        // Add the last value
        values.append(currentValue.trimmed());
        
        // Remove quotes from values if they exist
        for (QString& value : values) {
            if (value.startsWith('"') && value.endsWith('"')) {
                value = value.mid(1, value.length() - 2);
            }
        }
    }
    
    QVariant getValue(int column) const {
        if (column >= 0 && column < values.size()) {
            return values[column];
        }
        return QVariant();
    }
};

#endif // CSVENTRY_H
