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
	placeholder: ''
};

ClassicEditor.create(document.querySelector('#editor'), editorConfig).then( editor => {
	window.projectEditor = editor;
});
ClassicEditor.create(document.querySelector('#profile-editor'), editorConfig).then( editor => {
	window.profileEditor = editor;
});
