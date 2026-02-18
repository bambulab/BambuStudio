var projectName = "";
var projectPictures = [
  // {
  //   "filepath": "",
  //   "filename": "",
  //   "size": 0
  // }
];
var projectEditorData;
var bomAccessories = []; //like projectPictures structure
var assemblyAccessories = []; //like projectPictures structure
var otherAccessories = []; //like projectPictures structure
var profileName = "";
var profilePictures = [];
var profileEditorData;
var bakRequestData;
function deepCloneList(list) {
  return JSON.parse(JSON.stringify(list || []));
}
function safeDecode(value) {
  if (!value) return '';
  try {
    return decodeURIComponent(value);
  } catch (err) {
    return value;
  }
}

function getBase64ImageFormat(base64Str) {
  if (!isBase64String(base64Str)) return null;
  const trimmed = base64Str.trim();
  const dataUrlMatch = trimmed.match(/^data:(.*?);base64,/i);
  let payload = trimmed;
  if (dataUrlMatch) {
    const mime = (dataUrlMatch[1] || '').toLowerCase();
    payload = trimmed.slice(dataUrlMatch[0].length);
    if (mime.indexOf('image/png') >= 0) return 'png';
    if (mime.indexOf('image/jpeg') >= 0 || mime.indexOf('image/jpg') >= 0) return 'jpg';
  }
  const cleanPayload = payload.replace(/\s+/g, '');
  try {
    const header = atob(cleanPayload.slice(0, 16));
    const bytes = header.split('').map(ch => ch.charCodeAt(0));
    if (bytes[0] === 0x89 && bytes[1] === 0x50 && bytes[2] === 0x4E && bytes[3] === 0x47) return 'png';
    if (bytes[0] === 0xFF && bytes[1] === 0xD8 && bytes[2] === 0xFF) return 'jpg';
  } catch (err) {
    return null;
  }
  return null;
}

function normalizeCoverName(value) {
  const decoded = safeDecode(value);
  if (!decoded) return '';
  const segments = decoded.split(/[/\\]/);
  return segments[segments.length - 1];
}
function normalizeRequestPayload(raw) {
  const safe = raw || {};
  const model = safe.model || {};
  const file = safe.file || {};
  const profile = safe.profile || {};
  return {
    model: {
      name: model.name || "",
      description: model.description || "",
      preview_img: deepCloneList(model.preview_img),
    },
    file: {
      BOM: deepCloneList(file.BOM),
      Assembly: deepCloneList(file.Assembly),
      Other: deepCloneList(file.Other),
    },
    profile: {
      name: profile.name || "",
      description: profile.description || "",
      preview_img: deepCloneList(profile.preview_img),
    },
  };
}
function getCurrentRequestData() {
  getProjectName();
  getProjectDescription();
  getProfileName();
  getProfilePictures();
  getProfileDescription();
  return {
    model: {
      name: encodeURIComponent(projectName || ""),
      description: encodeURIComponent(projectEditorData || ""),
      preview_img: deepCloneList(projectPictures),
    },
    file: {
      BOM: deepCloneList(bomAccessories),
      Assembly: deepCloneList(assemblyAccessories),
      Other: deepCloneList(otherAccessories),
    },
    profile: {
      name: encodeURIComponent(profileName || ""),
      description: encodeURIComponent(profileEditorData || ""),
      preview_img: deepCloneList(profilePictures),
    },
  };
}
const editorUploadRequests = new Map();
$(function () {
  TranslatePage();
  addAccessoryBtnListener();
  addPictureUploadListener(
    'projectImageInput',
    'imageList',
    projectPictures,
    ['image/jpeg', 'image/png', 'image/gif', 'image/webp', 'image/jpg'],
    4 * 1024 * 1024
  );
  addAccessoryUploadListener(
    'bom-input',
    'bom-list',
    bomAccessories,
    {
      '.xls': 10 * 1024 * 1024,
      '.xlsx': 10 * 1024 * 1024,
      '.pdf': 50 * 1024 * 1024
    },
    10
  );
  addAccessoryUploadListener(
    'assembly-guide-input',
    'assembly-list',
    assemblyAccessories,
    {
      '.jpg': 10 * 1024 * 1024,
      '.png': 10 * 1024 * 1024,
      '.pdf': 50 * 1024 * 1024
    },
    25
  );
  addAccessoryUploadListener(
    'other-input',
    'other-list',
    otherAccessories,
    {
      '.txt': 10 * 1024 * 1024,
    },
    10
  );
  addPictureUploadListener(
    'ProfileImageInput',
    'profileImageList',
    profilePictures,
    ['image/jpeg', 'image/png', 'image/gif', 'image/webp', 'image/jpg'],
    4 * 1024 * 1024
  );
  // TestProjectData.model.preview_img=[];
  // updateInfo(TestProjectData);
  RequestProjectInfo();
});
function isFormEmpty() {
  getProjectName();
  getProjectDescription();
  getProfileName();
  getProfilePictures();
  getProfileDescription();
  const noBasics = !projectName.trim() && !projectEditorData?.trim();
  const noProjectPics = projectPictures.length === 0;
  const noAttachments = bomAccessories.length === 0 &&
                         assemblyAccessories.length === 0 &&
                         otherAccessories.length === 0;
  const noProfileInfo = !profileName.trim() &&
                        !profileEditorData?.trim() &&
                        profilePictures.length === 0;
  return noBasics && noProjectPics && noAttachments && noProfileInfo;
}
function isChange() {
  if (!bakRequestData && isFormEmpty()) {
    return false;
  }
  const baseline = normalizeRequestPayload(bakRequestData);
  const current = normalizeRequestPayload(getCurrentRequestData());
  return JSON.stringify(baseline) !== JSON.stringify(current);
}
function saveInfo() {
  let modelData = {};
  getProjectName();
  if (!projectName) {
    showToast("The project name is empty.");
    return;
  }
  modelData["name"] = encodeURIComponent(projectName);
  if (projectPictures.length <= 0) {
    showToast("The project pictures is empty.");
    return;
  }
  modelData["preview_img"] = projectPictures;
  getProjectDescription();
  if (!projectEditorData) {
    showToast("The project description is empty.");
    return;
  }
  modelData["description"] = encodeURIComponent(projectEditorData);
  let fileData = {
    BOM: bomAccessories,
    Assembly: assemblyAccessories,
    Other: otherAccessories
  };
  getProfileName();
  getProfilePictures();
  getProfileDescription();
  let profileData = {
    name: encodeURIComponent(profileName),
    preview_img: profilePictures,
    description: encodeURIComponent(profileEditorData)
  };
  let updateData = {
    model: modelData,
    file: fileData,
    profile: profileData
  };
  var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="update_3mf_info";
  tSend['model'] = updateData;
		
	SendWXMessage( JSON.stringify(tSend) );	
}

function updateInfo(p3MF) {
  projectName = DOMPurify.sanitize(decodeURIComponent(p3MF.model.name));
  projectPictures.length = 0;
  Array.prototype.push.apply(projectPictures, p3MF.model.preview_img || []);
  let projectCover = normalizeCoverName(p3MF.model.cover_img || "");
  for (let i = 0; i < projectPictures.length; i++) {
    const pictureName = normalizeCoverName(projectPictures[i].filename);
    if (projectCover && pictureName === projectCover){
      const [item] = projectPictures.splice(i, 1);
      projectPictures.unshift(item);
      break;
    }
  }
  projectEditorData = decodeURIComponent(p3MF.model.description) || '';
  bomAccessories.length = 0;
  Array.prototype.push.apply(bomAccessories, p3MF.file.BOM || []);
  assemblyAccessories.length = 0;
  Array.prototype.push.apply(assemblyAccessories, p3MF.file.Assembly || []);
  otherAccessories.length = 0;
  Array.prototype.push.apply(otherAccessories, p3MF.file.Other || []);
  profileName = DOMPurify.sanitize(decodeURIComponent(p3MF.profile.name));
  profilePictures.length = 0;
  Array.prototype.push.apply(profilePictures, p3MF.profile.preview_img || []);
  let profileCover = normalizeCoverName(p3MF.profile.cover_img || "");
  for (let i = 0; i < profilePictures.length; i++) {
    const pictureName = normalizeCoverName(profilePictures[i].filename);
    if (profileCover && pictureName === profileCover){
      const [item] = profilePictures.splice(i, 1);
      profilePictures.unshift(item);
      break;
    }
  }
  profileEditorData = decodeURIComponent(p3MF.profile.description) || '';
  setProjectName();
  setProjectPictrues();
  setProjectDescription();
  setAccessor();
  setProfileName();
  setProfilePictures();
  setProfileDescription();
}
function setProjectName() {
	$("#projectNameInput").val(projectName);
}
function getProjectName() {
	projectName = $("#projectNameInput").val();
}
function setProjectPictrues() {
  setPictures('imageList', projectPictures);
}
function getProjectPictures() {
  return projectPictures;
}
function setProjectDescription() {
  if (window.projectEditor) {
    projectEditor.setData(projectEditorData || '');
  }
}
function getProjectDescription() {
  if (window.projectEditor) {
    projectEditorData = projectEditor.getData();
  }
}
function setAccessor() {
  setAccessories('bom-list', bomAccessories);
  setAccessories('assembly-list', assemblyAccessories);
  setAccessories('other-list', otherAccessories);
}
function setProfileName() {
  $("#ProfileNameInput").val(profileName);
}
function getProfileName() {
  profileName = $("#ProfileNameInput").val();
}
function setProfilePictures() {
  setPictures('profileImageList', profilePictures);
}
function getProfilePictures() {
  return profilePictures;
}
function setProfileDescription() {
  if (window.profileEditor) {
    profileEditor.setData(profileEditorData || '');
  }
}
function getProfileDescription() {
  if (window.profileEditor) {
    profileEditorData = profileEditor.getData();
  }
}
//Pictures tool
function setPictures(id, picturesList) {
  let updateHtml = "";
  for (let i = 0; i < picturesList.length; i++) {
    let pic_filepath = picturesList[i].filepath;
    if(picturesList[i].base64) {
      pic_filepath = picturesList[i].base64;
    }
    if (i == 0) {
      let html = `<div class="imagePreview" data-index="${i}" style="background-image: url('${pic_filepath}')">
  <img src="img/img_del.svg" />
  <div class="modelCover">Model cover</div>
</div>`;
      updateHtml = html + updateHtml;
    }else {
      let html = `<div class="imagePreview" data-index="${i}" style="background-image: url('${pic_filepath}')">
  <img src="img/img_del.svg" />
  <div class="setModelCover">Set as cover</div>
</div>`;
      updateHtml += html;
    }
  }
  $(`#${id}`).html(updateHtml);
  $(`#${id}`).off('click', 'img');
  $(`#${id}`).on('click', 'img', function (event) {
    let index = parseInt($(this).parent().data('index'));
    removePictureAt(index, picturesList);
    setPictures(id, picturesList);
  });
  $(`#${id}`).off('click', '.setModelCover');
  $(`#${id}`).on('click', '.setModelCover', function (event) {
    let index = parseInt($(this).parent().data('index'));
    const [item] = picturesList.splice(index, 1);
    picturesList.unshift(item);
    setPictures(id, picturesList);
  });
}
function removePictureAt(index, picturesList) {
  if (index < 0 || index >= picturesList.length) {
    return;
  }
  picturesList.splice(index, 1);  
}
function addPictureUploadListener(inputId, showPictrueId, picturesList, allowTypes, maxSize, maxCount) {
  const input = document.getElementById(inputId);
  // input.removeEventListener('click');
  input.addEventListener('click', () => {
    input.value = '';
  });
  // input.removeEventListener('change');
  input.addEventListener('change', function (event) {
    const [file] = event.target.files;
    if (!file) return;
    // Validate file type
    if (allowTypes && !allowTypes.includes(file.type)) {
      showToast(GetCurrentTextByKey("t144"));
      return;
    }
    // Validate file size
    if (maxSize && file.size > maxSize) {
      showToast(GetCurrentTextByKey("t145"));
      return;
    }
    if (picturesList.length >= maxCount) {
      showToast(GetCurrentTextByKey("t146"));
      return;
    }
    uploadFileToCpp(file).then(result => {
      if (result && result.path) {
        fileToThumbnailBase64(file).then((base64) => {
          picturesList.push({
          "filepath": result.path,
          "filename": file.name,
          "size": file.size,
          "base64": base64
          });
          setPictures(showPictrueId, picturesList);
          // showToast('add file1'+projectPictures.length);
        });
        
      }
    }).catch(error => {
      showToast(GetCurrentTextByKey("t147"));
    });
  });
}
//accessories tool
function addAccessoryBtnListener() {
  $("#accessories-btn").on("click", function (e) {
    e.stopPropagation();
    $(".accessory-rule-wrapper").toggle();
  });
  $(document).on('click', function (e) {
  if (!$(e.target).closest('#accessories-btn, .accessory-rule-wrapper').length) {
    $('.accessory-rule-wrapper').hide();
  }
});1
}
function setAccessories(id, accessoriesList) {
  let updateHtml = "";
  if (accessoriesList.length > 0) {
    $(`#${id}`).prev().show();
  }else {
    $(`#${id}`).prev().hide();
  }
  for (let i = 0; i < accessoriesList.length; i++) {
    let acc_filepath = accessoriesList[i].filepath;
    let type = "";
    if (accessoriesList[i].type) {
      type = getFileTail(accessoriesList[i].type);
    }else {
      type = getFileType(acc_filepath);
    }
    let iconPath = `img/icon_${type}.svg`;
    let html = `<div class="attachment" data-index="${i}"><img class="attachment-icon" src="${iconPath}">${decodeURIComponent(accessoriesList[i].filename)}<img class="attachment-delete" src="img/del.svg"></div>`;
    updateHtml += html;
  }
  $(`#${id}`).html(updateHtml);
  $(`#${id}`).children('.attachment').each(function(idx) {
    $(this).data('path', accessoriesList[idx]?.filepath || '');
  });
  $(`#${id}`).prev().children('label').text(accessoriesList.length);
  $(`#${id}`).off('click', '.attachment-delete');
  $(`#${id}`).on('click', '.attachment-delete', function (event) {
    event.stopPropagation();
    let index = parseInt($(this).parent().data('index'));
    removeAccessoryAt(index, accessoriesList);
    setAccessories(id, accessoriesList);
  });
  $(`#${id}`).off('click', '.attachment');
  $(`#${id}`).on('click', '.attachment', function (event) {
    if ($(event.target).closest('.attachment-delete').length) return;
    const path = $(this).data('path');
    OnClickOpenFile(event, path);
  });
}
function removeAccessoryAt(index, accessoriesList) {
  if (index < 0 || index >= accessoriesList.length) {
    return;
  }
  accessoriesList.splice(index, 1);  
}
function getFileTail(filetype) {
  switch(filetype) {
    case 'application/pdf':
      return 'pdf';
    case 'text/plain':
      return 'txt';
    case 'image/jpeg':
      return 'jpg';
    case 'image/png':
      return 'png';
    case 'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet':
      return 'xcl';
    case 'application/vnd.ms-excel':
      return 'xcl';
    default:
      return '';
  }
}
function getFileType(filepath) {
  let parts = filepath.split('.');
  if (parts.length > 1) {
    switch (parts[parts.length - 1].toLowerCase()) {
      case 'pdf':
        return 'pdf';
      case 'txt':
        return 'txt';
      case 'jpg':
        return 'jpg';
      case 'jpet':
        return 'jpg';
      case 'png':
        return 'png';
      case 'xlsx':
      case 'xls':
        return 'xcl';

    }
  }
  return getBase64ImageFormat(filepath);
}
function addAccessoryUploadListener(inputId, showAccessoryId, accessoriesList, allowTypes, maxCount) {
  const input = document.getElementById(inputId);
  input.addEventListener('click', () => {
    input.value = '';
  });
  input.addEventListener('change', function (event) {
    const [file] = event.target.files;
    if (!file) return;
    const lowerName = file.name.toLowerCase();
    const matchedExt = Object.keys(allowTypes).find(ext => lowerName.endsWith(ext));
    if (!matchedExt) {
      showToast(GetCurrentTextByKey("t144"));
      return;
    }
    const maxSize = allowTypes[matchedExt];
    if (maxSize && file.size > maxSize) {
      showToast(GetCurrentTextByKey("t145"));
      return;
    }
    if (accessoriesList.length >= maxCount) {
      showToast(GetCurrentTextByKey("t146"));
      return;
    }
    uploadFileToCpp(file).then(result => {
      if (result && result.path) {
        accessoriesList.push({
          "filepath": result.path,
          "filename": file.name,
          "size": file.size,
          "type": file.type
        });
        setAccessories(showAccessoryId, accessoriesList);
      }
    }).catch(error => {
      showToast(GetCurrentTextByKey("t147"));
    });
  });
}
//common tool
function generateSequenceId() {
  return `${Date.now()}_${Math.random().toString(36).slice(2, 10)}`;
}
function uploadFileToCpp(file) {
  return new Promise((resolve, reject) => {
    if (!(file instanceof File)) {
      reject(new Error('invalid file object'));
      return;
    }
    const reader = new FileReader();
    const sequenceId = generateSequenceId();
    reader.onload = () => {
      let result = reader.result;
      let base64Payload = '';
      if (typeof result === 'string') {
        const commaIndex = result.indexOf(',');
        base64Payload = commaIndex >= 0 ? result.substring(commaIndex + 1) : result;
      } else if (result instanceof ArrayBuffer) {
        const bytes = new Uint8Array(result);
        let binary = '';
        const chunk = 0x8000;
        for (let i = 0; i < bytes.length; i += chunk) {
          binary += String.fromCharCode.apply(null, bytes.subarray(i, i + chunk));
        }
        base64Payload = btoa(binary);
      } else {
        reject(new Error('unable to read file content'));
        return;
      }
      const message = {
        sequence_id: sequenceId,
        command: 'editor_upload_file',
        data: {
          filename: file.name || '',
          size: file.size || 0,
          type: file.type || '',
          base64: base64Payload
        }
      };
      editorUploadRequests.set(sequenceId, { resolve, reject });
      const canCallHost =
        typeof window !== 'undefined' &&
        typeof window.wx === 'object' &&
        typeof window.wx.postMessage === 'function';
      if (typeof SendWXMessage === 'function' && canCallHost) {
        try {
          SendWXMessage(JSON.stringify(message));
        } catch (error) {
          editorUploadRequests.delete(sequenceId);
          reject(error);
        }
      } else {
        editorUploadRequests.delete(sequenceId);
        const fallbackPath = URL.createObjectURL(file);
        resolve({
          path: fallbackPath,
          isMock: true
        });
      }
    };
    reader.onerror = () => {
      reject(reader.error || new Error('failed to read file'));
    };
    reader.readAsDataURL(file);
  });
}
function fileToThumbnailBase64(file, maxWidth = 200, quality = 0.85) {
  return new Promise((resolve, reject) => {
    if (!file || !file.type.startsWith('image/')) {
      reject(new Error('select a valid image file'));
      return;
    }
    const reader = new FileReader();
    reader.onload = e => {
      const img = new Image();
      img.crossOrigin = 'anonymous'; 
      img.src = e.target.result;
      img.onload = () => {
        const canvas = document.createElement('canvas');
        const ctx = canvas.getContext('2d');
        const scale = Math.min(1, maxWidth / img.width);
        const width = img.width * scale;
        const height = img.height * scale;
        canvas.width = width;
        canvas.height = height;
        if (file.type === 'image/png' || file.type === 'image/webp') {
          ctx.clearRect(0, 0, width, height);
        } else {
          ctx.fillStyle = '#fff';
          ctx.fillRect(0, 0, width, height);
        }
        ctx.drawImage(img, 0, 0, width, height);
        const outputType = ['image/png', 'image/jpeg', 'image/webp'].includes(file.type)
          ? file.type
          : 'image/png';
        const base64 = canvas.toDataURL(outputType, quality);
        resolve(base64);
      };
      img.onerror = () => reject(new Error('invalid image file'));
    };
    reader.onerror = reject;
    reader.readAsDataURL(file);
  });
}
function handleEditorMessage(rawMessage) {
  let payload = rawMessage;
  if (typeof rawMessage === 'string') {
    try {
      payload = JSON.parse(rawMessage); 
    } catch (error) {
      return;
    }
  }
  if (!payload || typeof payload !== 'object') {
    return;
  }
  const command = payload.command;
  if (command === 'editor_upload_file_result') {
    const sequenceId = payload.sequence_id;
    if (!sequenceId || !editorUploadRequests.has(sequenceId)) {
      return;
    }
    const { resolve, reject } = editorUploadRequests.get(sequenceId);
    editorUploadRequests.delete(sequenceId);
    if (payload.error) {
      reject(new Error(payload.error));
      return;
    }
    resolve(payload.data || {});
    return;
  }
  if (command === 'save_project') {
    saveInfo();
    return;
  }
  if (command === 'discard_project') {
    window.location.href = 'index.html';
    return;
  }
  if (command === 'update_3mf_info_result') {
    if (payload.error) {
      showToast(payload.error);
      return;
    }
    bakRequestData = JSON.parse(JSON.stringify(getCurrentRequestData()));
    showToast(payload.message || 'Saved successfully.');
    window.location.href = "index.html";
  }
}
function OnClickOpenFile( evt, strFullPath ) {
  if (!strFullPath) return;
  if (evt && $(evt.target).closest('.attachment-delete').length) {
    return;
  }
  if(isBase64String(strFullPath)) {
    showBase64ImageLayer(strFullPath);
    return;
  }
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="open_3mf_accessory";
	tSend['accessory_path']=strFullPath;
	
	SendWXMessage( JSON.stringify(tSend) );
	SendWXDebugInfo('----open accessory:  '+strFullPath);
}
function returnBtn() {
  if (isChange()) {
    confirmSave();
  }else {
    if (bakRequestData) {
      window.location.href = "index.html";
    }else {
      window.location.href = "black.html";
    }
    
  }
}
function confirmSave() {
  var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="request_confirm_save_project";
		
	SendWXMessage( JSON.stringify(tSend) );	
}
function RequestProjectInfo()
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="request_3mf_info";
		
	SendWXMessage( JSON.stringify(tSend) );		
}
function HandleStudio(pVal)
{
	let strCmd=pVal['command'];
	
	if(strCmd=='show_3mf_info')
	{
		updateInfo( pVal['model'] );
    bakRequestData = JSON.parse(JSON.stringify(getCurrentRequestData()));
	}
}

window.HandleEditor = handleEditorMessage;
