/*
 * Copyright (C) 2014 Aaron Seigo <aseigo@kde.org>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#include <unistd.h>

#include <QCoreApplication>
#include <QDebug>

#include "module.h"
// #include "jsonlistener.h"
#include "repl/repl.h"

/*
 * modes of operation:
 *
 *   1. called with no commands: start the REPL and listen for JSON on stin
 *   2. called with -: listen for JSON on stdin
 *   3. called with commands: try to match to syntx
 */

int main(int argc, char *argv[])
{
    // load all modules
    Module::loadModules();

    const bool interactive = isatty(fileno(stdin));
    const bool startRepl = (argc == 1) && interactive;
    //TODO: make a json command parse cause that would be awesomesauce
    const bool startJsonListener = !startRepl &&
                                   (argc == 2 && qstrcmp(argv[1], "-") == 0);
    qDebug() << "state at startup is" << interactive << startRepl << startJsonListener;

    QCoreApplication app(argc, argv);
    app.setApplicationName("funq");

    if (startRepl || startJsonListener) {
        if (startRepl) {
            Repl *repl = new Repl;
            QObject::connect(repl, &QStateMachine::finished,
                             repl, &QObject::deleteLater);
            QObject::connect(repl, &QStateMachine::finished,
                             &app, &QCoreApplication::quit);
        }

        if (startJsonListener) {
//        JsonListener listener(syntax);
        }

        return app.exec();
    }

    QStringList commands = app.arguments();
    commands.removeFirst();
    return Module::run(commands);
}
