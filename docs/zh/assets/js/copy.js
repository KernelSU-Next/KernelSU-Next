function copyToClipboard(button) {
    var code = button.previousElementSibling.textContent;
    navigator.clipboard.writeText(code).then(() => {
        button.textContent = "Copied!";
        setTimeout(() => button.textContent = "Copy", 2000);
    });
}