pipeline {
    agent none

    environment {
        CONAN_USER_HOME_SHORT = 'None'
        OSP_CONAN_CREDS = credentials('jenkins-osp-conan-creds')
        COSIM_CONAN_CHANNEL = "${env.BRANCH_NAME}".take(51).replaceAll("/", "_")
    }

    options { checkoutToSubdirectory('libcosim') }

    stages {

        stage('Build') {
            parallel {
                stage('Windows') {
                    agent { label 'windows' }

                    environment {
                        CONAN_USER_HOME = "${env.SLAVE_HOME}/conan-repositories/${env.EXECUTOR_NUMBER}"
                    }
                    
                    stages {
                        stage('Configure Conan') {
                            steps {
                                sh 'conan remote add osp https://osp-conan.azurewebsites.net/artifactory/api/conan/public --force'
                                sh 'conan remote add bincrafters https://api.bintray.com/conan/bincrafters/public-conan --force'
                                sh 'conan user -p $OSP_CONAN_CREDS_PSW -r osp $OSP_CONAN_CREDS_USR'
                            }
                        }
                        stage('Build Debug') {
                            steps {
                                dir('debug-build') {
                                    bat 'conan install ../libcosim -s build_type=Debug -b missing -u'
                                    bat 'conan build ../libcosim'
                                    bat 'conan package ../libcosim -pf package/windows/debug'
                                }
                            }
                        }
                        stage('Build Release') {
                            steps {
                                dir('release-build') {
                                    bat 'conan install ../libcosim -s build_type=Release -b missing -u'
                                    bat 'conan build ../libcosim'
                                    bat 'conan package ../libcosim -pf package/windows/release'
                                }
                            }
                        }
                        stage ('Test Debug') {
                            steps {
                                dir('debug-build') {
                                    bat 'ctest -C Debug -T Test --no-compress-output --test-output-size-passed 307200 || true'
                                }
                            }
                            post {
                                always{
                                    xunit (
                                        testTimeMargin: '30000',
                                        thresholdMode: 1,
                                        thresholds: [ skipped(failureThreshold: '0'), failed(failureThreshold: '0') ],
                                        tools: [ CTest(pattern: 'debug-build/Testing/**/Test.xml') ]
                                    )
                                }
                                success {
                                    dir('debug-build') {
                                        sh "conan remove libcosim --force"
                                        sh "conan export-pkg ../libcosim osp/${COSIM_CONAN_CHANNEL} -pf package/windows/debug --force"
                                        sh "conan upload libcosim/*@osp/${COSIM_CONAN_CHANNEL} --all -r=osp --confirm --force"
                                    }
                                    dir('debug-build/package') {
                                        archiveArtifacts artifacts: '**',  fingerprint: false
                                    }
                                }
                                cleanup {
                                    dir('debug-build/Testing') {
                                        deleteDir();
                                    }
                                }
                            }
                        }
                        stage ('Test Release') {
                            steps {
                                dir('release-build') {
                                    bat 'ctest -C Release -T Test --no-compress-output --test-output-size-passed 307200 || true'
                                }
                            }
                            post {
                                always{
                                    xunit (
                                        testTimeMargin: '30000',
                                        thresholdMode: 1,
                                        thresholds: [ skipped(failureThreshold: '0'), failed(failureThreshold: '0') ],
                                        tools: [ CTest(pattern: 'release-build/Testing/**/Test.xml') ]
                                    )
                                }
                                success {
                                    dir('release-build') {
                                        sh "conan remove libcosim --force"
                                        sh "conan export-pkg ../libcosim osp/${COSIM_CONAN_CHANNEL} -pf package/windows/release --force"
                                        sh "conan upload libcosim/*@osp/${COSIM_CONAN_CHANNEL} --all -r=osp --confirm --force"
                                    }
                                    dir('release-build/package') {
                                        archiveArtifacts artifacts: '**',  fingerprint: false
                                    }
                                }
                                cleanup {
                                    dir('release-build/Testing') {
                                        deleteDir();
                                    }
                                }
                            }
                        }
                    }
                }
                stage('Windows with FMU-proxy support') {
                    agent { label 'windows' }

                    environment {
                        CONAN_USER_HOME = "${env.SLAVE_HOME}/conan-repositories/${env.EXECUTOR_NUMBER}"
                    }

                    stages {
                        stage('Configure Conan') {
                            steps {
                                sh 'conan remote add osp https://osp-conan.azurewebsites.net/artifactory/api/conan/public --force'
                                sh 'conan remote add bincrafters https://api.bintray.com/conan/bincrafters/public-conan --force'
                                sh 'conan user -p $OSP_CONAN_CREDS_PSW -r osp $OSP_CONAN_CREDS_USR'
                            }
                        }
                        stage('Build Release') {
                            steps {
                                dir('release-build-fmuproxy') {
                                    bat 'conan install ../libcosim -s build_type=Release -o fmuproxy=True -b missing -u'
                                    bat 'conan build ../libcosim'
                                    bat 'conan package ../libcosim -pf package/windows/release'
                                }
                            }
                        }
                        stage ('Test Release') {
                            steps {
                                dir('release-build-fmuproxy') {
                                    bat 'ctest -C Release -T Test --no-compress-output --test-output-size-passed 307200 || true'
                                }
                            }
                            post {
                                always{
                                    xunit (
                                        testTimeMargin: '30000',
                                        thresholdMode: 1,
                                        thresholds: [ skipped(failureThreshold: '0'), failed(failureThreshold: '0') ],
                                        tools: [ CTest(pattern: 'release-build-fmuproxy/Testing/**/Test.xml') ]
                                    )
                                }
                                success {
                                    dir('release-build-fmuproxy') {
                                        sh "conan remove libcosim --force"
                                        sh "conan export-pkg ../libcosim osp/${COSIM_CONAN_CHANNEL} -pf package/windows/release --force"
                                        sh "conan upload libcosim/*@osp/${COSIM_CONAN_CHANNEL} --all -r=osp --confirm --force"
                                    }
                                    dir('release-build-fmuproxy/package') {
                                        archiveArtifacts artifacts: '**',  fingerprint: false
                                    }
                                }
                                cleanup {
                                    dir('release-build-fmuproxy/Testing') {
                                        deleteDir();
                                    }
                                }
                            }
                        }
                    }
                }
                stage ( 'Linux with Conan' ) {
                    agent {
                        dockerfile {
                            filename 'Dockerfile.conan-build'
                            dir 'libcosim/.dockerfiles'
                            label 'linux && docker'
                            args '-v ${SLAVE_HOME}/conan-repositories/${EXECUTOR_NUMBER}:/conan_repo'
                        }
                    }

                    environment {
                        CONAN_USER_HOME = '/conan_repo'
                    }

                    stages {
                        stage('Configure Conan') {
                            steps {
                                sh 'conan remote add osp https://osp-conan.azurewebsites.net/artifactory/api/conan/public --force'
                                sh 'conan remote add bincrafters https://api.bintray.com/conan/bincrafters/public-conan --force'
                                sh 'conan user -p $OSP_CONAN_CREDS_PSW -r osp $OSP_CONAN_CREDS_USR'
                            }
                        }
                        stage('Build Debug') {
                            steps {
                                dir('debug-build-conan') {
                                    sh 'conan install ../libcosim -s compiler.libcxx=libstdc++11 -s build_type=Debug -b missing -u'
                                    sh 'conan build ../libcosim'
                                    sh 'conan package ../libcosim -pf package/linux/debug'
                                }
                            }
                        }
                        stage('Build Release') {
                            steps {
                                dir('release-build-conan') {
                                    sh 'conan install ../libcosim -s compiler.libcxx=libstdc++11 -s build_type=Release -b missing -u'
                                    sh 'conan build ../libcosim'
                                    sh 'conan package ../libcosim -pf package/linux/release'
                                }
                            }
                        }
                        stage ('Test Debug') {
                            steps {
                                dir('debug-build-conan') {
                                    sh '. ./activate_run.sh && ctest -C Debug -T Test --no-compress-output --test-output-size-passed 307200 || true'
                                }
                            }
                            post {
                                always{
                                    xunit (
                                        testTimeMargin: '30000',
                                        thresholdMode: 1,
                                        thresholds: [ skipped(failureThreshold: '0'), failed(failureThreshold: '0') ],
                                        tools: [ CTest(pattern: 'debug-build-conan/Testing/**/Test.xml') ]
                                    )
                                }
                                success {
                                    dir('debug-build-conan') {
                                        sh "conan remove libcosim --force"
                                        sh "conan export-pkg ../libcosim osp/${COSIM_CONAN_CHANNEL} -pf package/linux/debug --force"
                                        sh "conan upload libcosim/*@osp/${COSIM_CONAN_CHANNEL} --all -r=osp --confirm --force"
                                    }
                                    dir('debug-build-conan/package') {
                                        archiveArtifacts artifacts: '**',  fingerprint: false
                                    }
                                }
                                cleanup {
                                    dir('debug-build-conan/Testing') {
                                        deleteDir();
                                    }
                                }
                            }
                        }
                        stage ('Test Release') {
                            steps {
                                dir('release-build-conan') {
                                    sh '. ./activate_run.sh && ctest -C Release -T Test --no-compress-output --test-output-size-passed 307200 || true'
                                }
                            }
                            post {
                                always{
                                    xunit (
                                        testTimeMargin: '30000',
                                        thresholdMode: 1,
                                        thresholds: [ skipped(failureThreshold: '0'), failed(failureThreshold: '0') ],
                                        tools: [ CTest(pattern: 'release-build-conan/Testing/**/Test.xml') ]
                                    )
                                }
                                success {
                                    dir('release-build-conan') {
                                        sh "conan remove libcosim --force"
                                        sh "conan export-pkg ../libcosim osp/${COSIM_CONAN_CHANNEL} -pf package/linux/release --force"
                                        sh "conan upload libcosim/*@osp/${COSIM_CONAN_CHANNEL} --all -r=osp --confirm --force"
                                    }
                                    dir('release-build-conan/package') {
                                        archiveArtifacts artifacts: '**',  fingerprint: false
                                    }
                                }
                                cleanup {
                                    dir('release-build-conan/Testing') {
                                        deleteDir();
                                    }
                                }
                            }
                        }
                    }
                }
                stage ( 'Linux with Conan & FMU-proxy support' ) {
                    agent {
                        dockerfile {
                            filename 'Dockerfile.conan-build'
                            dir 'libcosim/.dockerfiles'
                            label 'linux && docker'
                            args '-v ${SLAVE_HOME}/conan-repositories/${EXECUTOR_NUMBER}:/conan_repo'
                        }
                    }

                    environment {
                        CONAN_USER_HOME = '/conan_repo'
                    }

                    stages {
                        stage('Configure Conan') {
                            steps {
                                sh 'conan remote add osp https://osp-conan.azurewebsites.net/artifactory/api/conan/public --force'
                                sh 'conan remote add bincrafters https://api.bintray.com/conan/bincrafters/public-conan --force'
                                sh 'conan user -p $OSP_CONAN_CREDS_PSW -r osp $OSP_CONAN_CREDS_USR'
                            }
                        }
                        stage('Build Release') {
                            steps {
                                dir('release-build-conan-fmuproxy') {
                                    sh 'conan install ../libcosim -s compiler.libcxx=libstdc++11 -s build_type=Release -o fmuproxy=True -b missing -u'
                                    sh 'conan build ../libcosim'
                                    sh 'conan package ../libcosim -pf package/linux/release'
                                }
                            }
                        }
                        stage ('Test Release') {
                            steps {
                                dir('release-build-conan-fmuproxy') {
                                    sh '. ./activate_run.sh && ctest -C Release -T Test --no-compress-output --test-output-size-passed 307200 || true'
                                }
                            }
                            post {
                                always{
                                    xunit (
                                        testTimeMargin: '30000',
                                        thresholdMode: 1,
                                        thresholds: [ skipped(failureThreshold: '0'), failed(failureThreshold: '0') ],
                                        tools: [ CTest(pattern: 'release-build-conan-fmuproxy/Testing/**/Test.xml') ]
                                    )
                                }
                                success {
                                    dir('release-build-conan-fmuproxy') {
                                        sh "conan remove libcosim --force"
                                        sh "conan export-pkg ../libcosim osp/${COSIM_CONAN_CHANNEL} -pf package/linux/release --force"
                                        sh "conan upload libcosim/*@osp/${COSIM_CONAN_CHANNEL} --all -r=osp --confirm --force"
                                    }
                                    dir('release-build-conan-fmuproxy/package') {
                                        archiveArtifacts artifacts: '**',  fingerprint: false
                                    }
                                }
                                cleanup {
                                    dir('release-build-conan-fmuproxy/Testing') {
                                        deleteDir();
                                    }
                                }
                            }
                        }
                    }
                }
                stage ( 'Linux with Docker' ) {
                    agent {
                        dockerfile {
                            filename 'Dockerfile.build'
                            dir 'libcosim/.dockerfiles'
                            label 'linux && docker'
                        }
                    }

                    stages {
                        stage('Build Debug') {
                            steps {
                                dir('debug-build') {
                                    sh 'cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=../install/linux/debug -DLIBCOSIM_USING_CONAN=FALSE -DLIBCOSIM_BUILD_PRIVATE_APIDOC=ON ../libcosim'
                                    sh 'cmake --build .'
                                    sh 'cmake --build . --target install'
                                }
                            }
                        }
                        stage('Build Release') {
                            steps {
                                dir('release-build') {
                                    sh 'cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=../install/linux/release -DLIBCOSIM_USING_CONAN=FALSE -DLIBCOSIM_BUILD_PRIVATE_APIDOC=ON ../libcosim'
                                    sh 'cmake --build .'
                                    sh 'cmake --build . --target install'
                                }
                            }
                        }
                        stage ('Test Debug') {
                            steps {
                                dir('debug-build') {
                                    sh 'ctest -C Debug -T Test --no-compress-output --test-output-size-passed 307200 || true'
                                }
                            }
                        }
                        stage ('Test Release') {
                            steps {
                                dir('release-build') {
                                    sh 'ctest -C Release -T Test --no-compress-output --test-output-size-passed 307200 || true'
                                }
                            }
                        }
                    }
                    post {
                        always{
                            xunit (
                                testTimeMargin: '30000',
                                thresholdMode: 1,
                                thresholds: [ skipped(failureThreshold: '0'), failed(failureThreshold: '0') ],
                                tools: [ CTest(pattern: '*-build/Testing/**/Test.xml') ]
                            )
                        }
                        success {
                            dir('install') {
                                archiveArtifacts artifacts: '**',  fingerprint: false
                            }
                        }
                        cleanup {
                            dir('debug-build/Testing') {
                                deleteDir();
                            }
                            dir('release-build/Testing') {
                                deleteDir();
                            }
                        }
                    }
                }
            }
        }
    }
}
