
> gradle.properties

{
    echo 'android.experimental.enableNewResourceShrinker.preciseShrinking=true'
    echo 'android.enableAppCompileTimeRClass=true'
    echo 'android.useAndroidX=true'
    echo KEYSTORE_PASSWORD=$MY_KEYSTORE_PASSWORD
    echo KEY_ALIAS=$MY_KEY_ALIAS
    echo KEY_PASSWORD=$MY_KEY_PASSWORD
    echo KEYSTORE_FILE='key.jks'
    echo 'org.gradle.parallel=true'
    echo 'org.gradle.vfs.watch=true'
    echo 'org.gradle.jvmargs=-Xmx2048m'
    echo 'android.r8.maxWorkers=4'
} >> gradle.properties

./gradlew clean assembleDebug

> gradle.properties

{
    echo 'android.experimental.enableNewResourceShrinker.preciseShrinking=true'
    echo 'android.enableAppCompileTimeRClass=true'
    echo 'android.useAndroidX=true'
    echo 'org.gradle.jvmargs=-Xmx2048m'
    echo 'org.gradle.parallel=true'
    echo 'org.gradle.vfs.watch=true'
    echo 'android.r8.maxWorkers=4'
} >> gradle.properties