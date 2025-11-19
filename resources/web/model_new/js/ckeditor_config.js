const {
	ClassicEditor,
	Autosave,
	Essentials,
	Paragraph,
	ImageUtils,
	ImageEditing,
	Link,
	List,
	Alignment,
	Bold,
	Italic,
	Underline,
	AutoLink,
	Heading
} = window.CKEDITOR;

const LICENSE_KEY =
	'GPL';

const CKEDITOR_LANGUAGE_MAP = {
	en: 'en',
	zh_CN: 'zh-cn',
	ja_JP: 'ja',
	it_IT: 'it',
	fr_FR: 'fr',
	de_DE: 'de',
	hu_HU: 'hu',
	es_ES: 'es',
	sv_SE: 'sv',
	cs_CZ: 'cs',
	nl_NL: 'nl',
	uk_UA: 'uk',
	ru_RU: 'ru',
	tr_TR: 'tr',
	pt_BR: 'pt-br',
	ko_KR: 'ko',
	pl_PL: 'pl'
};

function detectEditorLanguage() {
	let lang = typeof GetQueryString === 'function' ? GetQueryString('lang') : null;
	const langStorageKey = typeof LANG_COOKIE_NAME !== 'undefined' ? LANG_COOKIE_NAME : 'BambuWebLang';
	try {
		if (lang && typeof localStorage !== 'undefined') {
			localStorage.setItem(langStorageKey, lang);
		}
	} catch (err) {}
	if (!lang) {
		try {
			if (typeof localStorage !== 'undefined') {
				lang = localStorage.getItem(langStorageKey);
			}
		} catch (err) {
			lang = null;
		}
	}
	if (!lang && typeof navigator !== 'undefined') {
		lang = navigator.language || navigator.userLanguage;
	}
	if (!lang) {
		return 'en';
	}
	const normalizedUnderscore = lang.replace('-', '_');
	if (CKEDITOR_LANGUAGE_MAP[normalizedUnderscore]) {
		return CKEDITOR_LANGUAGE_MAP[normalizedUnderscore];
	}
	if (CKEDITOR_LANGUAGE_MAP[lang]) {
		return CKEDITOR_LANGUAGE_MAP[lang];
	}
	return normalizedUnderscore.replace('_', '-').toLowerCase();
}

const editorLanguage = detectEditorLanguage();
const script = document.createElement('script');
script.src = `../include/ckeditor5/translations/${editorLanguage}.umd.js`;

const editorConfig = {
	toolbar: {
		items: [
			'heading',
			'|',
			'bold',
			'italic',
			'underline',
			'|',
			'alignment',
			'|',
			'bulletedList',
			'numberedList',
      '|',
			'link',
      '|',
      'undo',
			'redo',
		],
		shouldNotGroupWhenFull: false
	},
	plugins: [Alignment, AutoLink, Autosave, Bold, Essentials, Heading, ImageEditing, ImageUtils, Italic, Link, List, Paragraph, Underline],
	heading: {
		options: [
			{
				model: 'paragraph',
				title: 'Paragraph',
				class: 'ck-heading_paragraph'
			},
			{
				model: 'heading1',
				view: 'h1',
				title: 'Heading 1',
				class: 'ck-heading_heading1'
			},
			{
				model: 'heading2',
				view: 'h2',
				title: 'Heading 2',
				class: 'ck-heading_heading2'
			},
			{
				model: 'heading3',
				view: 'h3',
				title: 'Heading 3',
				class: 'ck-heading_heading3'
			},
			{
				model: 'heading4',
				view: 'h4',
				title: 'Heading 4',
				class: 'ck-heading_heading4'
			},
			{
				model: 'heading5',
				view: 'h5',
				title: 'Heading 5',
				class: 'ck-heading_heading5'
			},
			{
				model: 'heading6',
				view: 'h6',
				title: 'Heading 6',
				class: 'ck-heading_heading6'
			}
		]
	},
	initialData:
		'',
	licenseKey: LICENSE_KEY,
	link: {
		addTargetToExternalLinks: true,
		defaultProtocol: 'https://',
		decorators: {
			toggleDownloadable: {
				mode: 'manual',
				label: 'Downloadable',
				attributes: {
					download: 'file'
				}
			}
		}
	},
	placeholder: '',
	language: editorLanguage
};

script.onload = () => {
	ClassicEditor.create(document.querySelector('#editor'), editorConfig).then( editor => {
		window.projectEditor = editor;
	});
	ClassicEditor.create(document.querySelector('#profile-editor'), editorConfig).then( editor => {
		window.profileEditor = editor;
	});
};

document.head.appendChild(script);