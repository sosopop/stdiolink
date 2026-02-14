import React from 'react';
import { useTranslation } from 'react-i18next';
import { Dropdown, Button, MenuProps } from 'antd';
import { GlobalOutlined, CheckOutlined } from '@ant-design/icons';

export const LanguageSwitcher: React.FC = () => {
    const { i18n } = useTranslation();

    const handleMenuClick: MenuProps['onClick'] = (e) => {
        i18n.changeLanguage(e.key);
    };

    const currentLang = i18n.language || 'en';

    const items: MenuProps['items'] = [
        {
            key: 'en',
            label: 'English',
            icon: currentLang.startsWith('en') ? <CheckOutlined /> : null,
        },
        {
            key: 'zh',
            label: '简体中文',
            icon: currentLang === 'zh' ? <CheckOutlined /> : null,
        },
        {
            key: 'zh-TW',
            label: '繁體中文',
            icon: currentLang === 'zh-TW' ? <CheckOutlined /> : null,
        },
        {
            key: 'ja',
            label: '日本語',
            icon: currentLang.startsWith('ja') ? <CheckOutlined /> : null,
        },
        {
            key: 'ko',
            label: '한국어',
            icon: currentLang.startsWith('ko') ? <CheckOutlined /> : null,
        },
        {
            key: 'fr',
            label: 'Français',
            icon: currentLang.startsWith('fr') ? <CheckOutlined /> : null,
        },
        {
            key: 'de',
            label: 'Deutsch',
            icon: currentLang.startsWith('de') ? <CheckOutlined /> : null,
        },
        {
            key: 'es',
            label: 'Español',
            icon: currentLang.startsWith('es') ? <CheckOutlined /> : null,
        },
        {
            key: 'ru',
            label: 'Русский',
            icon: currentLang.startsWith('ru') ? <CheckOutlined /> : null,
        },
    ];

    return (
        <Dropdown menu={{ items, onClick: handleMenuClick }} placement="bottomRight" arrow>
            <Button type="text" icon={<GlobalOutlined />} />
        </Dropdown>
    );
};
