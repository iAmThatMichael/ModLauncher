#include "stdafx.h"

Dvar::Dvar(dvar_s dvar, QTreeWidget* dvarTree) : dvar(dvar)
{
	const auto dvarSetting = QString("dvar_%1").arg(dvar.name);
	const auto settings = QSettings{};

	auto* item = new QTreeWidgetItem(dvarTree, QStringList() << dvar.name);
	item->setText(0, dvar.name);
	item->setToolTip(0, dvar.description);

	QCheckBox* checkBox;
	QSpinBox* spinBox;
	QLineEdit* textBox;

	switch (this->dvar.type)
	{
	case DVAR_VALUE_BOOL:
		checkBox = new QCheckBox();
		checkBox->setChecked(settings.value(dvarSetting, false).toBool());
		checkBox->setToolTip("Boolean value, check to enable or uncheck to disable.");
		dvarTree->setItemWidget(item, 1, checkBox);
		break;
	case DVAR_VALUE_INT:
		spinBox = new QSpinBox();
		spinBox->setValue(settings.value(dvarSetting, 0).toInt());
		spinBox->setToolTip("Integer value, min to max any number.");
		spinBox->setMaximum(dvar.maxValue);
		spinBox->setMinimum(dvar.minValue);
		spinBox->setFixedHeight(16); // need this
		dvarTree->setItemWidget(item, 1, spinBox);
		break;
	case DVAR_VALUE_STRING:
		textBox = new QLineEdit();
		textBox->setText(settings.value(dvarSetting, "").toString());
		textBox->setToolTip(QString("String value, leave this blank for it to not be used."));
		textBox->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
		dvarTree->setItemWidget(item, 1, textBox);
		break;
	}
}

dvar_s Dvar::findDvar(QString _dvarName, QTreeWidget* DvarTree, dvar_s* dvars, int DvarSize)
{
	auto dvar = dvar_s{};
	for (auto dvarIdx = 0; dvarIdx < DvarSize; dvarIdx++)
	{
		dvar = Dvar(dvars[dvarIdx], DvarTree).dvar;
		if (dvar.name == _dvarName)
			return dvar;
	}
	return dvar;
}

QString Dvar::setDvarSetting(dvar_s dvar, QCheckBox* checkBox)
{
	auto settings = QSettings{};
	settings.setValue(QString("dvar_%1").arg(dvar.name), checkBox->isChecked());

	return settings.value(QString("dvar_%1").arg(dvar.name)).toString() == "true" ? "1" : "0"; // another way to do this?
}

QString Dvar::setDvarSetting(dvar_s _dvar, QSpinBox* _spinBox)
{
	auto settings = QSettings{};
	settings.setValue(QString("dvar_%1").arg(_dvar.name), _spinBox->value());

	return settings.value(QString("dvar_%1").arg(_dvar.name)).toString();
}

QString Dvar::setDvarSetting(dvar_s _dvar, QLineEdit* _lineEdit)
{
	auto settings = QSettings{};
	settings.setValue(QString("dvar_%1").arg(_dvar.name), _lineEdit->text());

	return settings.value(QString("dvar_%1").arg(_dvar.name)).toString();
}
